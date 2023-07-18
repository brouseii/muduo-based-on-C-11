#include "Channel.h"
#include "EventLoop.h"
#include "Logger.h"

#include <sys/epoll.h>

const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = EPOLLIN | EPOLLPRI;
const int Channel::kWriteEvent = EPOLLOUT;

// EventLoop: ChannelList Poller
Channel::Channel(EventLoop *loop, int fd)
    : loop_(loop), fd_(fd), events_(0), revents_(0), index_(-1), tied_(false)
{ }

Channel::~Channel()
{ }

// channel的tie方法什么时候调用过？
// 一个TcpConnection新连接创建的时候 TcpConnection => Channel 
void Channel::tie(const std::shared_ptr<void> &obj)
{
    tie_ = obj;
    tied_ = true;
}

/**
 * 当改变channel所表示fd的events事件后，update负责在poller里面更改fd相应的事件epoll_ctl
 *
 * EventLoop ==> ChannelLists + Poller，即channel需要通过EventLoop才能访问poller
 * ，即 Channel:update/remove --> EventLoop:updateChannel/removeChannel --> Poller
 */ 
void Channel::update()
{
    // 通过channel所属的EventLoop，调用poller的相应方法，注册fd的events事件
    loop_->updateChannel(this);
}

// 在channel所属的EventLoop中，删除当前的channel
void Channel::remove()
{
    loop_->removeChannel(this);
}

/* 当fd_得到poller的事件后，处理事件，即调用相应的回调函数 */
void Channel::handleEvent(Timestamp receiveTime)
{
    if (tied_)
    {
		// 将weak_ptr提升为shared_ptr，并通过判断返回值来衡量tie_是否还存在
        std::shared_ptr<void> guard = tie_.lock();
        if (guard)
        {
            handleEventWithGuard(receiveTime);
        }
    }
    else
    {
        handleEventWithGuard(receiveTime);
    }
}

// 根据poller通知的channel发生的具体事件， 由channel负责调用具体的回调操作
void Channel::handleEventWithGuard(Timestamp receiveTime)
{
    LOG_INFO("channel handleEvent revents:%d\n", revents_);

	// 在使用epoll机制进行I/O多路复用时，当文件描述符上出现EPOLLHUP事件时，通常意味着连接已经被对端关闭，或者一些错误导致连接异常断开。
    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN))
    {
        if (closeCallback_)
        {
            closeCallback_();
        }
    }

    if (revents_ & EPOLLERR)
    {
        if (errorCallback_)
        {
            errorCallback_();
        }
    }

	/*  
        EPOLLPRI是Linux系统中epoll的一种事件类型，它表示有一个高优先级的带外(out-of-band)数据可供读取。
        带外数据是一种特殊类型的数据，它不是按照普通数据流来发送和接收的，而是可以在普通数据的外部被发送和接收。
        这种数据通常用于向应用程序传递紧急信息或控制命令，因此具有更高的优先级。 
	*/
    if (revents_ & (EPOLLIN | EPOLLPRI))
    {
        if (readCallback_)
        {
            readCallback_(receiveTime);
        }
    }

    if (revents_ & EPOLLOUT)
    {
        if (writeCallback_)
        {
            writeCallback_();
        }
    }
}