#include "Poller.h"
#include "Channel.h"

Poller::Poller(EventLoop *loop)
    : ownerLoop_(loop)
{
}

// 判断当前poller中，是否有该channel
/*
    std::unordered_map<int, Channel*> channels_;
    <key,value> <==> <sockfd,其所属的channel>
*/
bool Poller::hasChannel(Channel *channel) const
{
    auto it = channels_.find(channel->fd());
    return it != channels_.end() && it->second == channel;
}