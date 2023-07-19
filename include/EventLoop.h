#pragma once

#include <functional>
#include <vector>
#include <atomic>
#include <memory>
#include <mutex>

#include "Logger.h"
#include "noncopyable.h"
#include "Timestamp.h"
#include "CurrentThread.h"

class Channel;
class Poller;

// 事件的循环类：主要包含了Channel和Poller（epoll的抽象）两大类
class EventLoop : noncopyable
{
public:
    using Functor = std::function<void()>;

    EventLoop();
    ~EventLoop();   // Force out-line dtor, for unique_ptr members.

    // 开启事件循环：
    // must be called in the same thread as creation of the object
    void loop();
    // 退出事件循环：
    // This is not 100% thread safe, if you call through a raw pointer.
    // better to call through shared_ptr<EventLoop> for 100% safety.
    void quit();

    // Time when poll returns, usually means data arrival.
    Timestamp pollReturnTime() const { return pollReturnTime_; }
    
    // 在当前loop中执行cb
    void runInLoop(Functor cb);
	
    // 将cb放入队列中，唤醒loop所在的线程，执行cb
    void queueInLoop(Functor cb);

    // 用来唤醒loop所在的线程的
    void wakeup();

    // EventLoop的这些方法，需要调用Poller的方法
    void updateChannel(Channel *channel);
    void removeChannel(Channel *channel);
    bool hasChannel(Channel *channel);

    // 判断EventLoop对象是否在自己的线程里面
    bool isInLoopThread() const { return threadId_ ==  CurrentThread::tid(); }
	void assertInLoopThread() 
    {
        if (!isInLoopThread())
        {
            LOG_FATAL("[EventLoop::assertInLoopThread] ==> EventLoop was created in threadId_ = %d, current thread id = %d\n", threadId_, CurrentThread::tid());
        }
    }
private:
    void handleRead(); // wake up
    void doPendingFunctors(); // 执行回调

    using ChannelList = std::vector<Channel*>;

    std::atomic_bool looping_;  // 原子操作，通过CAS实现的
    std::atomic_bool quit_; // 标识退出loop循环
    
    const pid_t threadId_; // 记录当前loop所在线程的id

    Timestamp pollReturnTime_; // poller返回发生事件的channels的时间点
    std::unique_ptr<Poller> poller_;

    /*
        主要的作用：当mainloop获取一个新用户的channel
        ，并通过轮询算法获取一个subloop
        ，通过该成员唤醒subloop处理channel
    */  
    int wakeupFd_;   // 通过eventfd()来进行线程之间的notify
    std::unique_ptr<Channel> wakeupChannel_;

    ChannelList activeChannels_;

    std::atomic_bool callingPendingFunctors_; // 标识当前loop是否有需要执行的回调操作
    std::vector<Functor> pendingFunctors_; // 存储loop需要执行的所有的回调操作
    std::mutex mutex_; // 互斥锁，用来保护上面vector容器的线程安全操作
};
