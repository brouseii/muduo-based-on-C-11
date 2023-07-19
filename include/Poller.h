#pragma once

#include "noncopyable.h"
#include "Timestamp.h"

#include <vector>
#include <unordered_map>

class Channel;
class EventLoop;

// muduo网络库中，多路事件分发器的核心IO复用模块。
// Poller含有纯虚函数，故是一个抽象类，不能构造类对象。
class Poller : noncopyable
{
public:
    using ChannelList = std::vector<Channel*>;

    Poller(EventLoop *loop);
    virtual ~Poller() = default;

    // 给所有IO复用保留统一的接口
    /* Must be called in the loop thread. */
    // polls the I/O events
    virtual Timestamp poll(int timeoutMs, ChannelList *activeChannels) = 0;
    // Changes the interested I/O events
    virtual void updateChannel(Channel *channel) = 0;
    // Remove the channel, when it destructs.
    virtual void removeChannel(Channel *channel) = 0;
    
    // 判断参数channel是否在当前Poller当中
    bool hasChannel(Channel *channel) const;

    // EventLoop可以通过该接口，获取默认的IO复用的具体实现
    static Poller* newDefaultPoller(EventLoop *loop);
protected:
    // map的<key,value>分别对应<sockfd,其所属的channel>
    using ChannelMap = std::unordered_map<int, Channel*>;
    ChannelMap channels_;
private:
    EventLoop *ownerLoop_; // 定义Poller所属的事件循环EventLoop
};
