#include "EventLoop.h"
#include "Logger.h"
#include "Poller.h"
#include "Channel.h"

#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <memory>

/* 防止在一个线程中，创建多个EventLoop对象 */
// __thread是一个关键字，其修饰的全局变量t_loopInThisThread在每一个线程内都会有一个独立的实体（一般的全局变量都是被同一个进程中的多个线程所共享）。
__thread EventLoop *t_loopInThisThread = nullptr;

// 定义默认的Poller IO复用接口的超时时间
const int kPollTimeMs = 10000;

// 创建wakeupfd，用来notify唤醒subReactor处理新来的channel
int createEventfd()
{
    int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evtfd < 0)
    {
	// eventfd创建失败，一般不会失败，除非一个进程把文件描述符（Linux一个进程1024个最多）全用光了。
        LOG_FATAL("eventfd error:%d \n", errno);
    }
    return evtfd;
}

EventLoop::EventLoop()
    : looping_(false)
    , quit_(false)
    , callingPendingFunctors_(false)
    , threadId_(CurrentThread::tid())
    , poller_(Poller::newDefaultPoller(this))
    , wakeupFd_(createEventfd())   // 生成一个eventfd，每个EventLoop对象，都会有自己的eventfd
    , wakeupChannel_(new Channel(this, wakeupFd_))
{
    LOG_DEBUG("EventLoop created %p in thread %d \n", this, threadId_);
    if (t_loopInThisThread)
    {
	// 如果当前线程已经绑定了某个EventLoop对象（t_loopInThisThread != nullptr），那么该线程就无法创建新的EventLoop对象了。
        LOG_FATAL("Another EventLoop %p exists in this thread %d.\n", t_loopInThisThread, threadId_);
    }
    else
    {
	// 如果当前线程没有绑定EventLoop对象（t_loopInThisThread == nullptr），那么就让该指针变量指向EventLoop对象的地址。
        t_loopInThisThread = this;
    }

    // 设置wakeupfd的事件类型以及发生事件后的回调操作
    wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));
    // 每一个eventloop都将监听wakeupchannel的EPOLLIN读事件了
    wakeupChannel_->enableReading();
}

EventLoop::~EventLoop()
{
    wakeupChannel_->disableAll();  // 使channel对所有事件均丧失兴趣
    wakeupChannel_->remove();
    ::close(wakeupFd_);
    t_loopInThisThread = nullptr;
}

// 开启事件循环：持续循环的获取监听结果并根据结果调用处理函数
void EventLoop::loop()
{
    looping_ = true;
    quit_ = false;

    LOG_INFO("EventLoop %p start looping \n", this);

    while(!quit_)
    {
        activeChannels_.clear();  // 清空vector<Channel*>
		
	/* Poller监听哪些channel发生事件了，然后上报给EventLoop，并通知Channel处理相应的事件 */
        // 监听两类fd：一种是client的fd，一种wakeupfd
        pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
		
        for (Channel *channel : activeChannels_)
        {
            // Poller监听哪些channel发生事件了，然后上报给EventLoop，通知channel处理相应的事件
            channel->handleEvent(pollReturnTime_);
        }
		
        // 执行当前EventLoop事件循环需要处理的回调操作
        /**
         * IO线程 mainLoop accept fd《= channel subloop
         * mainLoop 事先注册一个回调cb（需要subloop来执行）    
         * wakeup subloop后，执行下面的方法（即执行之前mainloop注册在pendingFunctors中的cb操作）
        */ 
        doPendingFunctors();
    }

    LOG_INFO("EventLoop %p stop looping. \n", this);
    looping_ = false;
}

// 退出事件循环  1.loop在自己的线程中调用quit  2.在非loop的线程中，调用loop的quit
/*
                            mainloop
========= 通过读写wakeupfd_来进行通信（并非采用生产者消费者模型）=========
            subloop1        subloop2            subloop3 
*/
void EventLoop::quit()
{
    quit_ = true;

    // 在一个subloop(woker)中，调用了mainLoop(IO)的quit 
    if (!isInLoopThread())  
    {
	// 如果在其他线程中，调用了quit()，需要在先唤醒之后
	//，且该loop在执行完一次while循环后自动quit
        wakeup();
    }
}

// 该函数保证了cb这个函数对象一定是在其EventLoop线程中被调用，即在当前loop中执行cb
void EventLoop::runInLoop(Functor cb)
{
    if (isInLoopThread()) // 在当前的loop线程中，执行cb
    {
	// 如果当前调用runInLoop的线程正好是EventLoop的运行线程，则直接执行此函数。
        cb();
    }
    else 
    {
	// 如果在非当前线程中执行cb，则需要将cb放入队列中，并唤醒loop所在的线程来执行cb。
        queueInLoop(cb);
    }
}
// 把cb放入队列中，唤醒loop所在的线程，执行cb
void EventLoop::queueInLoop(Functor cb)
{
    {
        std::unique_lock<std::mutex> lock(mutex_);
        pendingFunctors_.emplace_back(cb);
    }

    // 唤醒相应的，需要执行上面回调操作的loop的线程了
    // callingPendingFunctors_为true表示当前loop正在执行回调，但当前loop又有了新的回调操作
    if (!isInLoopThread() || callingPendingFunctors_) 
    {
        wakeup(); // 唤醒loop所在线程
    }
}

void EventLoop::handleRead()
{
  uint64_t one = 1;
  ssize_t n = read(wakeupFd_, &one, sizeof(one));
  if (n != sizeof(one))
  {
    LOG_ERROR("EventLoop::handleRead() reads %lu bytes instead of 8", n);
  }
}
 
// 通过向wakeupfd_写一个数据，来唤醒loop所在的线程。
// 向wakeupfd_写一个数据，wakeupChannel就发生读事件，当前loop线程就会被唤醒
void EventLoop::wakeup()
{
    uint64_t one = 1;
    ssize_t n = write(wakeupFd_, &one, sizeof(one));
    if (n != sizeof(one))
    {
        LOG_ERROR("EventLoop::wakeup() writes %lu bytes instead of 8 \n", n);
    }
}

// EventLoop的这些方法，需要调用Poller的方法
void EventLoop::updateChannel(Channel *channel)
{
    poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel *channel)
{
    poller_->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel *channel)
{
    return poller_->hasChannel(channel);
}

// 执行回调操作：
void EventLoop::doPendingFunctors() 
{
    // 定义局部的functors，并与pendingFunctors_进行交换
    //，之后pendingFunctors_会变为空，mainloop可继续给其中添加cb
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;

    {
        std::unique_lock<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);
    }

    for (const Functor &functor : functors)
    {
        functor(); // 执行当前loop需要执行的回调操作cb
    }

    callingPendingFunctors_ = false;
}
