#include "EventLoopThread.h"
#include "EventLoop.h"


EventLoopThread::EventLoopThread(const ThreadInitCallback &cb, 
        const std::string &name)
        : loop_(nullptr)
        , exiting_(false)
        , thread_(std::bind(&EventLoopThread::threadFunc, this), name)
        , mutex_()
        , cond_()
        , callback_(cb)
{ }

EventLoopThread::~EventLoopThread()
{
    exiting_ = true;
    if (loop_ != nullptr)
    {
        loop_->quit();
        thread_.join();
    }
}

EventLoop* EventLoopThread::startLoop()
{
	// 启动底层的线程，并执行EventLoopThread::threadFunc()回调函数 
    thread_.start(); 

    EventLoop *loop = nullptr;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        while ( loop_ == nullptr )  // threadFunc()中还未给loop_赋值
        {
            cond_.wait(lock);    // 接到notify_one()的通知，会继续执行否则阻塞等到通知
        }
        loop = loop_;
    }
    return loop;
}

// 该方法在单独的线程中执行
void EventLoopThread::threadFunc()
{
	// 创建一个独立的eventloop，和上面的线程是一一对应的，one loop per thread
    EventLoop loop; 

    if (callback_)
    {
        callback_(&loop);
    }

    {
        std::unique_lock<std::mutex> lock(mutex_);
        loop_ = &loop;
        cond_.notify_one();
    }
	
	// EventLoop loop  => Poller.poll
    loop.loop(); 
	
	// 关闭loop_，即不再进行事件循环
    std::unique_lock<std::mutex> lock(mutex_);
    loop_ = nullptr;
}