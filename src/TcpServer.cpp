#include "TcpServer.h"
#include "Logger.h"
#include "TcpConnection.h"

#include <strings.h>
#include <functional>

// TcpServer对象中，loop_不能为空
static EventLoop* CheckLoopNotNull(EventLoop *loop)
{
    if (loop == nullptr)
    {
        LOG_FATAL("%s:%s:%d mainLoop is null! \n", __FILE__, __FUNCTION__, __LINE__);
    }
    return loop;
}

TcpServer::TcpServer(EventLoop *loop,
                const InetAddress &listenAddr,
                const std::string &nameArg,
                Option option)
                : loop_(CheckLoopNotNull(loop))
                , ipPort_(listenAddr.toIpPort())
                , name_(nameArg)
                , acceptor_(new Acceptor(loop, listenAddr, option == kReusePort))
                , threadPool_(new EventLoopThreadPool(loop, name_))
                , connectionCallback_()
                , messageCallback_()
                , nextConnId_(1)
                , started_(0)
{
    // 当有用户连接时，会执行TcpServer::newConnection回调
    acceptor_->setNewConnectionCallback(std::bind(&TcpServer::newConnection, this, std::placeholders::_1, std::placeholders::_2));
}

/* 彻底删除一个TcpConnection对象，必须要调用该对象的connecDestroyed()方法，执行完后才能释放该对象的堆内存。*/
TcpServer::~TcpServer()
{  
    for(auto &item : connections_)  // connections_类型为unordered_map<string, TcpConnectionPtr>，其中TcpConnectionPtr是指向TcpConnection的shared_ptr共享智能指针。
    {
        // 这个局部的shared_ptr智能指针对象，出右括号则会自动释放，即引用计数减一。
        TcpConnectionPtr conn(item.second);
        // 在MainEventLoop的TcpServer::~TcpServer()函数中，调用item.second.reset()，释放掉TcpServer中保存的该TcpConnection对象的智能指针。
        item.second.reset(); 
        
        /* 此时，只有“conn”持有TcpConnection对象，即引用计数为1 */
        // 让这个TcpConnection对象conn所属的SubEventLoop线程，执行TcpConnection::connectDestroyed()函数。
        conn->getLoop()->runInLoop(bind(&TcpConnection::connectDestroyed, conn));
        /* 此时，“conn”和“subEventLoop中的TcpConnection::connectDestroyed()成员函数的this指针”共同持有TcpConnection对象，即引用计数为2 */
    }
    /* 此时，只“subEventLoop中的TcpConnection::connectDestroyed()成员函数的this指针”持有TcpConnection对象，即引用计数为1。在该函数执行结束后，引用计数减为0，即会自动释放TcpConnection对象。*/
}

// 设置底层subloop的个数
void TcpServer::setThreadNum(int numThreads)
{
    threadPool_->setThreadNum(numThreads);
}

// 开启服务器监听   loop.loop()
void TcpServer::start()
{
    if (started_++ == 0) // 防止一个TcpServer对象被start多次
    {
        // 启动底层的loop线程池
        threadPool_->start(threadInitCallback_); 
		
	// 在当前loop中，执行Acceptor::listen()回调函数
        loop_->runInLoop(std::bind(&Acceptor::listen, acceptor_.get()));
    }
}

// 有一个新的客户端连接，acceptor会执行这个回调操作
void TcpServer::newConnection(int sockfd, const InetAddress &peerAddr)
{
	loop_->assertInLoopThread();
    /*
        根据“轮询算法”选择一个subloop（又称ioloop），
        1）唤醒subloop
        2）把当前connfd封装成channel分发给subloop
    */
    EventLoop *ioLoop = threadPool_->getNextLoop(); 
	std::string connName = name_ + "-" + ipPort_ + "#" + std::to_string(nextConnId_);
    ++nextConnId_;  
    LOG_INFO("TcpServer::newConnection [%s] - new connection [%s] from %s \n",
			name_.c_str(), connName.c_str(), peerAddr.toIpPort().c_str());

    // 通过sockfd获取其绑定的本机的ip地址和端口信息组成的InetAddress数据结构
    sockaddr_in local;
    memset(&local, 0, sizeof(local));
    socklen_t addrlen = sizeof(local);
    if (::getsockname(sockfd, (sockaddr*)&local, &addrlen) < 0)
    {
        LOG_ERROR("sockets::getLocalAddr");
    } 
    InetAddress localAddr(local);

    // 根据连接成功的sockfd，创建TcpConnection连接对象
    TcpConnectionPtr conn(new TcpConnection(
                            ioLoop,
                            connName,
                            sockfd,   // Socket Channel
                            localAddr,
                            peerAddr));
    connections_[connName] = conn;  // 将该连接<connName, conn>存放在ConnectionMap中
	
    // 给该连接设置各种的回调函数：
    // 下面的回调都是用户设置给TcpServer=>TcpConnection=>Channel=>Poller=>notify channel调用回调
    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setWriteCompleteCallback(writeCompleteCallback_);

    // 设置关闭连接的回调   conn->shutDown()
    conn->setCloseCallback(std::bind(&TcpServer::removeConnection, this, std::placeholders::_1));

    // 直接调用TcpConnection::connectEstablished
    ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
}
 
void TcpServer::removeConnection(const TcpConnectionPtr &conn)
{
    // 在mainEventLoop中执行removeConnectionInLoop()函数
    loop_->runInLoop(std::bind(&TcpServer::removeConnectionInLoop, this, conn));
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr &conn)
{
    LOG_INFO("TcpServer::removeConnectionInLoop [%s] - connection %s\n", 
        name_.c_str(), conn->name().c_str());
	
    connections_.erase(conn->name());
    EventLoop *ioLoop = conn->getLoop(); 
    // 在该TcpConnection所属的subEventLoop中执行connectDestroyed
    ioLoop->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
}
