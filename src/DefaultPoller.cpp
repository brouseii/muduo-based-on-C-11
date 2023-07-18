#include "Poller.h"
#include "EPollPoller.h"

#include <stdlib.h>

// EventLoop可以通过该接口，获取默认的I/O复用的具体实现
Poller* Poller::newDefaultPoller(EventLoop *loop)
{
    if (::getenv("MUDUO_USE_POLL"))
    {
        return nullptr; // 生成poll的实例
    }
    else
    {
        return new EPollPoller(loop); // 生成epoll的实例
    }
}
// 为了避免Poller基类，依赖派生类EpollPoller、PollPoller，采用的该解决方法