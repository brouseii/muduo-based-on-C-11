#include "EPollPoller.h"
#include "Logger.h"
#include "Channel.h"

#include <errno.h>
#include <unistd.h>
#include <cstring>

const int kNew = -1;     // channel未添加到poller中，即channel成员index_初始化时均为-1
const int kAdded = 1;   // channel已在poller中
const int kDeleted = 2;  // channel从poller中删除

// EPOLL_CLOEXEC是Linux系统中epoll_create1()函数的一个标志参数，它的作用是在创建一个新的epoll实例时设置其文件描述符为"close-on-exec"，即在执行exec()函数时自动关闭。
EPollPoller::EPollPoller(EventLoop *loop)
    : Poller(loop)
    , epollfd_(::epoll_create1(EPOLL_CLOEXEC))
    , events_(kInitEventListSize)  // vector<epoll_event>
{
    if (epollfd_ < 0)
    {
        LOG_FATAL("epoll_create error:%d \n", errno);
    }
}

EPollPoller::~EPollPoller() 
{
    ::close(epollfd_);
}

// polls the I/O events
Timestamp EPollPoller::poll(int timeoutMs, ChannelList *activeChannels)
{
    // 实际上应该用LOG_DEBUG输出日志更为合理
    LOG_INFO("[EpollPoller::%s] ==> fd total size = %d.\n", __FUNCTION__, channels_.size());
	
    /* int epoll_wait(int __epfd, epoll_event *__events, int __maxevents, int __timeout) */ 
    int numEvents = ::epoll_wait(epollfd_, &*events_.begin(), static_cast<int>(events_.size()), timeoutMs);
    int saveErrno = errno;
    Timestamp now(Timestamp::now());

    if (numEvents > 0)
    {
        LOG_INFO("%d events happened.\n", numEvents);
        fillActiveChannels(numEvents, activeChannels);
        if (numEvents == events_.size())
        {
	    // 此时，需要对vector<epoll_event> events_容器进行“2倍扩容”操作
            events_.resize(events_.size() * 2);
        }
    }
    else if (numEvents == 0)
    {
        LOG_DEBUG("%s timeout!\n", __FUNCTION__);
    }
    else
    {
		// error happen, log uncommon ones
        if (saveErrno != EINTR)
        {
            errno = saveErrno;
            LOG_ERROR("EPollPoller::poll() err!");
        }
    }
    return now;
}

/* Must be called in the loop thread. */
// Changes the interested I/O events
/*
            EventLoop
            |        |
    ChannelList     Poller
                      ||
                ChannelMap<fd,Channel*> channels_
*/
// Update the channel.
void EPollPoller::updateChannel(Channel *channel)
{
    const int index = channel->index();
    LOG_INFO("[EpollPoller::%s] ==> fd=%d events=%d index=%d.\n", __FUNCTION__, channel->fd(), channel->events(), index);

    if (index == kNew || index == kDeleted)
    {
	// a new one, add with EPOLL_CTL_ADD
        if (index == kNew)
        {
            int fd = channel->fd();
            channels_[fd] = channel;
        }

        channel->set_index(kAdded);
        update(EPOLL_CTL_ADD, channel);
    }
    else  // 此时，channel已经在poller上注册过了
    {
	// update existing one with EPOLL_CTL_MOD/DEL
        int fd = channel->fd();
        if (channel->isNoneEvent())
        {
            update(EPOLL_CTL_DEL, channel);
            channel->set_index(kDeleted);
        }
        else
        {
            update(EPOLL_CTL_MOD, channel);
        }
    }
}

// 从poller中删除channel
// Remove the channel, when it destructs.
void EPollPoller::removeChannel(Channel* channel) 
{
    int fd = channel->fd();
    channels_.erase(fd);

    LOG_INFO("[EPollPoller::%s] => fd=%d\n", __FUNCTION__, fd);
    
    int index = channel->index();
    if (index == kAdded)
    {
        update(EPOLL_CTL_DEL, channel);
    }
    channel->set_index(kNew);
}

// 填写活跃的连接
void EPollPoller::fillActiveChannels(int numEvents, ChannelList *activeChannels) const
{
    for (int i=0; i < numEvents; ++i)
    {
        Channel *channel = static_cast<Channel*>(events_[i].data.ptr);
        channel->set_revents(events_[i].events);
        activeChannels->push_back(channel); // EventLoop就拿到了它的poller给它返回的所有发生事件的channel列表了
    }
}
 
// 通过调用epoll_ctl()，完成对channel通道所监听事件在epollfd_上的operation操作
void EPollPoller::update(int operation, Channel *channel)
{
    /*
        #include <sys/epoll.h>
        int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event); 

        struct epoll_event {
            uint32_t events;  // 表示要监听的事件类型
            epoll_data_t data;  // 表示与事件相关的数据
        }; 

        typedef union epoll_data {
            void *ptr;
            int fd;
            uint32_t u32;
            uint64_t u64;
        } epoll_data_t;
    */ 
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    
    int fd = channel->fd();

    event.events = channel->events();
    event.data.fd = fd; 
    event.data.ptr = channel;
    
    if (::epoll_ctl(epollfd_, operation, fd, &event) < 0)
    {
        if (operation == EPOLL_CTL_DEL)
        {
            LOG_ERROR("epoll_ctl del error:%d\n", errno);
        }
        else
        {
            LOG_FATAL("epoll_ctl add/mod error:%d\n", errno);
        }
    }
}
