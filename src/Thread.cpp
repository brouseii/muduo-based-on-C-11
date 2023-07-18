#include "Thread.h"
#include "CurrentThread.h"

#include <semaphore.h>

// 初始化总线程数为0
std::atomic_int Thread::numCreated_(0);

Thread::Thread(ThreadFunc func, const std::string &name)
    : started_(false)
    , joined_(false)
    , tid_(0)
    , func_(std::move(func))
    , name_(name)
{
    setDefaultName();
}

Thread::~Thread()
{
    if (started_ && !joined_)
    {
		// 设置线程为分离状态
        thread_->detach(); 
    }
}

/* 使用到了linux中的信号量，避免主线程已执行完，但 “子线程还未成功创建” 或 “线程函数还未被成功调用” */
void Thread::start()  // 一个Thread对象，记录的就是一个新线程的详细信息
{
    started_ = true;
	
	// 定义并初始化一个信号量，并初始化为0
    sem_t sem;
    sem_init(&sem, false, 0);   // 这里是单个进程，故pshared=false

    // 新建并开启子线程
    thread_ = std::shared_ptr<std::thread>(new std::thread([&](){
        // 获取线程的tid值
        tid_ = CurrentThread::tid();
		
		// 这里会对信号量加一，主线程会收到信号量的变化
        sem_post(&sem);
		
        // 在新子线程中，执行线程函数
        func_(); 
    }));

    // 等待上面新子线程创建并执行线程函数后后，才能退出
    sem_wait(&sem);     // Wait for Sem being posted
}

void Thread::join()
{
    joined_ = true;
    thread_->join();
}

void Thread::setDefaultName()
{
    int num = ++numCreated_;
    if (name_.empty())
    {
        name_ = "Thread" + std::to_string(num);
    }
}