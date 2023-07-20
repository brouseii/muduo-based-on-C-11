#pragma once

#include "noncopyable.h"
#include "InetAddress.h"
#include "Callbacks.h"
#include "Buffer.h"
#include "Timestamp.h"

#include <memory>
#include <string>
#include <atomic>

class Channel;
class EventLoop;
class Socket;

/**
 * TcpServer => Acceptor => 有一个新用户连接，通过accept函数拿到connfd
 * => TcpConnection 设置回调 => Channel => Poller => Channel的回调操作
 * 
 */ 
class TcpConnection : noncopyable, public std::enable_shared_from_this<TcpConnection>
/**
 * enable_shared_from_this (C++11) 允许对象创建指代自身的 shared_ptr : 
*       1. operator= : 返回到 this 的引用 (受保护成员函数)
*       2. shared_from_this : 返回共享 *this 所有权的 shared_ptr (公开成员函数)
*       3. weak_from_this (C++17) : 返回共享 *this 所有权的 weak_ptr (公开成员函数)
 * 若一个类 T 继承 std::enable_shared_from_this<T> ，则会为该类 T 提供成员函数 shared_from_this。
 *      如：当 T 类型对象 t 被一个为名为 pt 的 std::shared_ptr<T> 类对象管理时，调用 T::shared_from_this 成员函数，将会返回一个新的 std::shared_ptr<T> 对象，它与 pt 共享 t 的所有权。
 */
 {
public:
    TcpConnection(EventLoop *loop, 
                const std::string &name, 
                int sockfd,
                const InetAddress& localAddr,
                const InetAddress& peerAddr);
    ~TcpConnection();

    EventLoop* getLoop() const { return loop_; }
    const std::string& name() const { return name_; }
    const InetAddress& localAddress() const { return localAddr_; }
    const InetAddress& peerAddress() const { return peerAddr_; }

    bool connected() const { return state_ == kConnected; }
	bool disconnected() const { return (state_ == kDisconnected); }

    // 发送数据
    void send(const std::string &buf);
    // 关闭当前连接
    void shutdown();    // not thread safe, no simultaneous calling
 
    // 设置回调函数：
    void setConnectionCallback(const ConnectionCallback& cb)
    { connectionCallback_ = cb; } 
    void setMessageCallback(const MessageCallback& cb)
    { messageCallback_ = cb; } 
    void setWriteCompleteCallback(const WriteCompleteCallback& cb)
    { writeCompleteCallback_ = cb; } 
    void setHighWaterMarkCallback(const HighWaterMarkCallback& cb, size_t highWaterMark)
    { highWaterMarkCallback_ = cb; highWaterMark_ = highWaterMark; } 
    void setCloseCallback(const CloseCallback& cb)
    { closeCallback_ = cb; }
	
    /* This two functions should be called only once. */
    // Called me when tcpServer accepts a new connection
    void connectEstablished();
    // Called me when TcpServer has remove me from its map
    void connectDestroyed();
private:
    enum StateE {kDisconnected, kConnecting, kConnected, kDisconnecting};
	const char* stateToString() const;
    void setState(StateE state) { state_ = state; }

    void handleRead(Timestamp receiveTime);
    void handleWrite();
    void handleClose();
    void handleError();
	
    // 由于应用层写的快，内核发送数据慢，故需要将待发送的数据先写入缓冲区，且设置了水位回调
    void sendInLoop(const void* message, size_t len);
    void shutdownInLoop();

    EventLoop *loop_; // 这里绝对不是baseLoop， 因为TcpConnection都是在subLoop里面管理的
    const std::string name_;
    std::atomic_int state_;   // atomic variable
    bool reading_;
	
    // Acceptor ==> mainloop、TcpConnection ==> subloop
    std::unique_ptr<Socket> socket_;
    std::unique_ptr<Channel> channel_;

    const InetAddress localAddr_;
    const InetAddress peerAddr_;
    
    // 各种回调函数：
    ConnectionCallback connectionCallback_; // 有新连接时的回调
    MessageCallback messageCallback_;       // 有读写消息时的回调
    WriteCompleteCallback writeCompleteCallback_; // 消息发送完成以后的回调
    HighWaterMarkCallback highWaterMarkCallback_;
    CloseCallback closeCallback_;
	
    size_t highWaterMark_;  // 设置水位线

    // 接收数据的缓冲区
    Buffer inputBuffer_; 
    // 发送数据的缓冲区（避免发送数据过快，导致数据丢失），通过水位线highWaterMark限制发送的数据量
    Buffer outputBuffer_;   // FIXME : use list<Buffer> as output buffer
};
