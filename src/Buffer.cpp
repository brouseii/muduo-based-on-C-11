#include "Buffer.h"

#include <errno.h>
#include <sys/uio.h>
#include <unistd.h>

/**
 * 接受客户端发送的存放在TCP接收缓冲区的数据，并存入buffer_中。
 * 从fd上读取数据  Poller工作在LT模式
 * Buffer缓冲区是有大小的！ 但是从fd上读数据的时候，却不知道tcp数据最终的大小。
*/ 
ssize_t Buffer::readFd(int fd, int* saveErrno)
{
    char extrabuf[65536] = {0}; // 栈上的内存空间  64K  初始化为0
   
    /* 
        The pointer iov points to an array of iovec structures:
        struct iovec 
        {
            void  *iov_base;    // Starting address 
            size_t iov_len;     // Number of bytes to transfer 
        }; 
    */
    struct iovec vec[2];
    
    const size_t writable = writableBytes(); // 这是Buffer底层缓冲区剩余的可写空间大小
    vec[0].iov_base = begin() + writerIndex_;  // 第一块缓冲区，为buffer_从writeIndex_开始的剩余可写的连续空间
    vec[0].iov_len = writable;

    vec[1].iov_base = extrabuf;                // 第二块缓冲区，自定义的栈区64K的连续空间
    vec[1].iov_len = sizeof(extrabuf);
    
    /*     
		Read data into the multiple buffers :   
		The readv() system call reads iovcnt buffers from the file associated  with  the file descriptor fd 
		into the buffers described by iov ("scatter input").
    */
    const int iovcnt = (writable < sizeof extrabuf) ? 2 : 1;
    const ssize_t n = ::readv(fd, vec, iovcnt);
    if (n < 0)
    {
        *saveErrno = errno;
    }
    // 此时，表示从fd的接收缓冲区，收到的n字节数据，已正常放入buffer_中，故只需调整writeIndex_即可。
    else if (n <= writable) // Buffer的可写缓冲区已经够存储读出来的数据了
    {
        writerIndex_ += n;
    }
    // 此时，表示从fd的接收缓冲区，收到的n字节数据，除了放入buffer_中的数据，还有n - writalbe字节的数据，存放在了extrabuf中。
    // 所以，需要将extrabuf中的数据转移到扩容后的buffer_中。
    else                    // extrabuf里面也写入了数据 
    {
        writerIndex_ = buffer_.size();
	// 从 writerIndex_开始写 n - writable 大小的数据，到buffer_中
        append(extrabuf, n - writable);  
    }

    return n;
}

// 将buffer_中的数据，写入TCP发送缓冲区，之后回传给客户端
ssize_t Buffer::writeFd(int fd, int* saveErrno)
{
    size_t n = ::write(fd, peek(), readableBytes());
    if (n < 0)
    {
        *saveErrno = errno;
    }
    else
    {
        saveErrno = nullptr;
    }
    return n;
}
