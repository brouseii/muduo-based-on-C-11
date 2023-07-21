#include "TcpConnection.h"
#include "Logger.h"
#include "Socket.h"
#include "Channel.h"
#include "EventLoop.h"

#include <functional>
#include <errno.h>
#include <sys/types.h>         
#include <sys/socket.h>
#include <strings.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <string>

// TcpConnection对象中，loop_不能为空
static EventLoop* CheckLoopNotNull(EventLoop *loop)
{
    if (loop == nullptr)
    {
        LOG_FATAL("%s:%s:%d TcpConnection Loop is null! \n", __FILE__, __FUNCTION__, __LINE__);
    }
    return loop;
}

TcpConnection::TcpConnection(EventLoop *loop, 
                const std::string &nameArg, 
                int sockfd,
                const InetAddress& localAddr,
                const InetAddress& peerAddr)
    : loop_(CheckLoopNotNull(loop))
    , name_(nameArg)
    , state_(kConnecting)
    , reading_(true)
    , socket_(new Socket(sockfd))
    , channel_(new Channel(loop, sockfd))
    , localAddr_(localAddr) , peerAddr_(peerAddr)
    , highWaterMark_(64*1024*1024) // 高水位标志：64M
{
    // 下面给channel设置相应的回调函数，poller给channel通知感兴趣的事件发生了，channel会回调相应的操作函数
    channel_->setReadCallback(
        std::bind(&TcpConnection::handleRead, this, std::placeholders::_1)
    );
    channel_->setWriteCallback(
        std::bind(&TcpConnection::handleWrite, this)
    );
    channel_->setCloseCallback(
        std::bind(&TcpConnection::handleClose, this)
    );
    channel_->setErrorCallback(
        std::bind(&TcpConnection::handleError, this)
    );

    LOG_INFO("TcpConnection::ctor[%s] at fd=%d\n", name_.c_str(), sockfd);
    socket_->setKeepAlive(true);
}


TcpConnection::~TcpConnection()
{
    LOG_INFO("TcpConnection::dtor[%s] at fd=%d state=%s.\n", name_.c_str(), channel_->fd(), stateToString());
}

// 发送消息
void TcpConnection::send(const std::string& msg)
{
    if (state_ == kConnected)
    {
        if (loop_->isInLoopThread())
        {
            sendInLoop(msg.c_str(), msg.size());
        }
        else
        {
            loop_->runInLoop(std::bind(&TcpConnection::sendInLoop, this, msg.c_str(), msg.size()));
        }
    }
}

// 由于应用层写的快，内核发送数据慢，故需要将待发送的数据先写入缓冲区，且设置了水位回调
void TcpConnection::sendInLoop(const void* data, size_t len)
{
    loop_->assertInLoopThread();
    ssize_t nwrote = 0;
    size_t remaining = len;
    bool faultError = false;

    // 之前调用过该connection的shutdown，不能再进行发送了
    if (state_ == kDisconnected)
    {
        LOG_ERROR("disconnected, give up writing!");
        return;
    }
 
    // if no thing in output queue, try writing directly
    // 此时，channel_第一次开始写数据，而且缓冲区没有待发送数据
    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0)
    {
        nwrote = ::write(channel_->fd(), data, len);
        if (nwrote >= 0)   // 成功发送了
        {
            remaining = len - nwrote;
            if (remaining == 0 && writeCompleteCallback_)
            {
                // 既然在这里数据全部发送完成，就不用再给channel设置epollout事件了
                loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
            }
        }
        else // nwrote < 0
        {
            nwrote = 0;
            if (errno != EWOULDBLOCK)
            {
                LOG_ERROR("TcpConnection::sendInLoop");
                if (errno == EPIPE || errno == ECONNRESET) // SIGPIPE  RESET
                {
                    faultError = true;
                }
            }
        }
    }

    // 说明当前这一次write，并没有把数据全部发送出去，剩余的数据需要保存到缓冲区当中，然后给channel
    // 注册epollout事件，poller发现tcp的发送缓冲区有空间，会通知相应的sock-channel，调用writeCallback_回调方法
    // 也就是调用TcpConnection::handleWrite方法，把发送缓冲区中的数据全部发送完成
    if (!faultError && remaining > 0) 
    {
        // 目前发送缓冲区剩余的待发送数据的长度
        size_t oldLen = outputBuffer_.readableBytes();
        if (oldLen + remaining >= highWaterMark_
            && oldLen < highWaterMark_
            && highWaterMarkCallback_)
        {
            loop_->queueInLoop(std::bind(highWaterMarkCallback_, shared_from_this(), oldLen+remaining));
        }
	// 将message[nwrote, nwrote+remaining]的数据，写入到outputbuffer_中
        outputBuffer_.append((char*)data + nwrote, remaining);
		
	// 向poller注册channel的写事件，否则poller不会给channel通知epollout
        if (!channel_->isWriting())
        {
            channel_->enableWriting();  
        }
    }
}

// 关闭连接
void TcpConnection::shutdown()
{
    if (state_ == kConnected)
    {
        setState(kDisconnecting);
        loop_->runInLoop(std::bind(&TcpConnection::shutdownInLoop, this));
    }
}

void TcpConnection::shutdownInLoop()
{
    if (!channel_->isWriting()) // 说明outputBuffer中的数据已经全部发送完成
    {
        socket_->shutdownWrite(); // 关闭写端
    }
}

/* This two functions should be called only once. */
// Called me when tcpServer accepts a new connection 
void TcpConnection::connectEstablished()  // 连接建立
{
    setState(kConnected);
    channel_->tie(shared_from_this());
    channel_->enableReading(); // 向poller注册channel的epollin事件

    // 新连接建立，执行回调
    connectionCallback_(shared_from_this());
}
// Called me when TcpServer has remove me from its map 
void TcpConnection::connectDestroyed()  // 连接销毁
{
    if (state_ == kConnected)
    {
        setState(kDisconnected);
        channel_->disableAll(); // 把channel的所有感兴趣的事件，从poller中del掉
        connectionCallback_(shared_from_this());  // 新连接建立，执行回调
    }
    channel_->remove(); // 把channel从poller中删除掉
}

void TcpConnection::handleRead(TimeStamp receiveTime)
{
    int savedErrno = 0;   // 保存读取拷贝的过程中发生的错误
    // 已建立连接的用户，有可读事件发生了，并将Tcp接收缓冲区数据拷贝到用户定义的缓冲区inputBuffer_中
    ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);  
    if(n > 0) 
    {
        // 从fd读到了数据，并且放在了inputBuffer_上，接着调用messageCallback_
        messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
    }
    else if(n == 0)
    {
        // 连接的客户端已关闭，这时会调用TcpConnection::handleClose()来处理连接关闭事件
        handleClose();
    }    
    else
    {
        // 读取时sockfd的接收缓冲区时发生了错误，调用TcpConnection::handleError( )来处理savedErrno的错误事件
        errno = savedErrno;
        LOG_ERROR("TcpConnection::handleRead");
        handleError();
        /* Moduo库只支持LT模式，所以读事件不会出现EAGAIN错误（EAGAIN错误是非阻塞IO调用时的一种常见错误），所以一旦出现错误肯定是比较不好的非正常错误。*/
    }
}

void TcpConnection::handleWrite()
{
    if (channel_->isWriting())
    {
        int savedErrno = 0;
        ssize_t n = outputBuffer_.writeFd(channel_->fd(), &savedErrno);
        if (n > 0)
        {
            outputBuffer_.retrieve(n);
	    // 此时，buffer_中的数据已经全部通过channel_->fd()被发送给了客户端
            if (outputBuffer_.readableBytes() == 0)
            {
                channel_->disableWriting();
                if (writeCompleteCallback_)
                {
                    // 唤醒loop_对应的thread线程，执行回调
                    loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
                }
                if (state_ == kDisconnecting)
                {
                    shutdownInLoop();
                }
            }
        }
        else
        {
            LOG_ERROR("TcpConnection::handleWrite");
        }
    }
    else
    {
        LOG_ERROR("TcpConnection fd=%d is down, no more writing \n", channel_->fd());
    }
}

// poller => channel::closeCallback => TcpConnection::handleClose
void TcpConnection::handleClose()
{
    LOG_INFO("TcpConnection::handleClose fd=%d state=%d \n", channel_->fd(), (int)state_);
    setState(kDisconnected);
    channel_->disableAll();

    TcpConnectionPtr connPtr(shared_from_this());
    connectionCallback_(connPtr); // 执行连接关闭的回调
    // must be the last line
    closeCallback_(connPtr); // 执行关闭连接的回调，本质上执行的是TcpServer::removeConnection回调方法
}

void TcpConnection::handleError()
{
    int optval;
    socklen_t optlen = sizeof(optval);
    int err = 0;
    if (::getsockopt(channel_->fd(), SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
    {
        err = errno;
    }
    else
    {
        err = optval;
    }
    LOG_ERROR("TcpConnection::handleError name:%s - SO_ERROR:%d \n", name_.c_str(), err);
}

const char* TcpConnection::stateToString() const 
{
    switch (state_)
    {
        case kDisconnected:
            return "kDisconnected";
        case kDisconnecting:
            return "kDisconnecting";
        case kConnected:
            return "kConnected";
        case kConnecting:
            return "kConnecting";
    }
}
