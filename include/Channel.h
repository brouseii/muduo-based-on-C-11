#pragma once

#include "noncopyable.h"
#include "Timestamp.h"

#include <functional>
#include <memory>

class EventLoop;

/**
 * 理清楚  EventLoop、Channel、Poller之间的关系  <==  Reactor模型上对应 Demultiplex
 * Channel 理解为通道，封装了sockfd和其感兴趣的event，如EPOLLIN、EPOLLOUT事件
 * 还绑定了poller返回的具体事件
 */ 
class Channel : noncopyable
{
public:
    using EventCallback = std::function<void()>;
    using ReadEventCallback = std::function<void(Timestamp)>;

    Channel(EventLoop *loop, int fd);
    ~Channel();

    /* 当fd_得到poller的事件后，处理事件，即调用相应的回调函数 */
    void handleEvent(Timestamp receiveTime);  

    // 初始化不同类型事件的回调函数对象
    void setReadCallback(ReadEventCallback cb) { readCallback_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
    void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }

    // 防止当channel被手动remove掉，channel还在执行回调操作
    void tie(const std::shared_ptr<void>&);

	// 返回该Channel监听的文件描述符
    int fd() const { return fd_; }
	// 返回该Channel监听的文件描述符所感兴趣的事件
    int events() const { return events_; }
	// 设置pollers返回的发生的事件
    int set_revents(int revt) { revents_ = revt; }

    // 设置fd相应的事件状态
    void enableReading() { events_ |= kReadEvent; update(); }
    void disableReading() { events_ &= ~kReadEvent; update(); }
    void enableWriting() { events_ |= kWriteEvent; update(); }
    void disableWriting() { events_ &= ~kWriteEvent; update(); }
    void disableAll() { events_ = kNoneEvent; update(); }

    // 返回fd当前的事件状态
    bool isNoneEvent() const { return events_ == kNoneEvent; }
    bool isWriting() const { return events_ & kWriteEvent; }
    bool isReading() const { return events_ & kReadEvent; }

    int index() { return index_; }
    void set_index(int idx) { index_ = idx; }

    // one loop per thread
    EventLoop* ownerLoop() { return loop_; }
	// 在channel所属的EventLoop对象loop_中，删除掉当前的channel
    void remove();
private:
    /* 
        当改变channel所表示的fd的事件events_后，update负责更改poller中相应的事件epoll_ctl
        EventLoop ==> ChannelLists + Poller 
    */
    void update();
    void handleEventWithGuard(Timestamp receiveTime);

    static const int kNoneEvent;
    static const int kReadEvent;
    static const int kWriteEvent;

    EventLoop *loop_; // 事件循环
    const int fd_;    // fd, Poller监听的对象
    int events_; // 注册fd感兴趣的事件
    int revents_; // poller返回的具体发生的事件
	
    int index_;// used by poller，表示该channel在poller中的状态{kNew=-1未在、kAdded=1已在、kDeleted=2已删除}
    // 每个channel初始化时，在poller中均为kNew状态，即index_=-1

    std::weak_ptr<void> tie_;
    bool tied_;

	// 因Channel可获知fd返回的发生的具体事件revents，所以它负责调用具体事件的回调操作
    //，故可在该类中注册各种读/写/异常/关闭回调函数
    ReadEventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
    EventCallback errorCallback_;
};

