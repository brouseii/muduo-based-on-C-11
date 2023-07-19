#pragma once

#include "noncopyable.h"
#include "Logger.h"
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>         
#include <sys/socket.h> 
#include <netinet/tcp.h>


class InetAddress;

// 封装socket fd
class Socket : noncopyable
{
public:
    explicit Socket(int sockfd)
        : sockfd_(sockfd)
    {}

    ~Socket();

    int fd() const { return sockfd_; }
	
    void bindAddress(const InetAddress &localaddr);  // 用sockfd_来绑定服务端的IP和Port
	
    void listen();                                   // 监听sockfd_套接字
	
    int accept(InetAddress *peeraddr);               // 接受客户端的连接
    // On success, 
    //          1、returns a non-negative integer that is a descriptor for the accepted socket, which has been set to non-blocking and close-on-exec.
    //          2、*peeraddr is assigned.
    // On error, -1 is returned, and *peeraddr is untouched.

    void shutdownWrite();    // 关闭服务器的写端

    /* 这四个函数，通过调用::setsockopt()方法，来设置sockfd_的属性 */ 
    void setTcpNoDelay(bool on);
    void setReuseAddr(bool on);
    void setReusePort(bool on);
    void setKeepAlive(bool on);
	
    // 通过sockfd_获取其绑定的IP+Port的sockaddr_in地址结构
    static struct sockaddr_in sockfd_To_SockAddr(int sockfd) 
    {
        struct sockaddr_in sockAddr;
        std::memset(&sockAddr, 0, sizeof(sockAddr));
        socklen_t sockAddrlen = (socklen_t)(sizeof(sockAddr));
        if (::getsockname(sockfd, (struct sockaddr*)(&sockAddr), &sockAddrlen) < 0)
        {
            LOG_ERROR("Socket::sockfd_To_SockAddr() is error.\n");
        }
        return sockAddr;
    } 
private:
    const int sockfd_;
};
