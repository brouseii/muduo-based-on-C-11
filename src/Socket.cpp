#include "Socket.h" 
#include "InetAddress.h" 

Socket::~Socket()
{
    close(sockfd_);
}

// abort if address in use
void Socket::bindAddress(const InetAddress &localaddr)
{
    if (0 != ::bind(sockfd_, (sockaddr*)localaddr.getSockAddr(), sizeof(sockaddr_in)))
    {
        LOG_FATAL("bind sockfd:%d fail \n", sockfd_);
    }
}
// abort if address in use
void Socket::listen()
{
    if (0 != ::listen(sockfd_, 1024))
    {
        LOG_FATAL("listen sockfd:%d fail \n", sockfd_);
    }
}
// On success, 
//          1、returns a non-negative integer that is a descriptor for the accepted socket, which has been set to non-blocking and close-on-exec.
//          2、*peeraddr is assigned.
// On error, -1 is returned, and *peeraddr is untouched.
int Socket::accept(InetAddress *peeraddr)
{
    /** 
     * Reactor模型：one loop per thread
     * poller + non-blocking IO
     * 注意：对返回的connfd没有设置非阻塞
     */ 
    sockaddr_in addr;
    socklen_t len = sizeof(addr);
    memset(&addr, 0, sizeof(addr));
    int connfd = ::accept4(sockfd_, (sockaddr*)&addr, &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (connfd >= 0)
    {
        peeraddr->setSockAddr(addr);
    }
    return connfd;
}

void Socket::shutdownWrite()
{
    if (::shutdown(sockfd_, SHUT_WR) < 0)
    {
        LOG_ERROR("shutdownWrite error");
    }
}

void Socket::setTcpNoDelay(bool on)
{
    int optval = on ? 1 : 0;
	// TCP_NODELAY，属于IPPROTO_TCP层
    ::setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));
}

/* SOL_SOCKET：表示选项属于套接字本身，适用于所有协议族 */
void Socket::setReuseAddr(bool on)
{
    int optval = on ? 1 : 0;
	// SO_REUSEADDR：允许地址重用，属于SOL_SOCKET层
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
}

void Socket::setReusePort(bool on)
{
    int optval = on ? 1 : 0;
	// SO_REUSEPORT：允许端口重用，属于SOL_SOCKET层
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
}

void Socket::setKeepAlive(bool on)
{
    int optval = on ? 1 : 0;
	// SO_KEEPALIVE：启用TCP心跳机制，属于SOL_SOCKET层
    ::setsockopt(sockfd_, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));
}