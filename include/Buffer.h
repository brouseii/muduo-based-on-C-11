#pragma once

#include "Logger.h"
#include <vector>
#include <string>
#include <algorithm>

// 网络库底层的缓冲器类型定义
// +-------------------+------------------+------------------+
// | prependable bytes |  readable bytes  |  writable bytes  |
// |                   |     (CONTENT)    |                  |
// +-------------------+------------------+------------------+
// |                   |                  |                  |
// 0      <=      readerIndex   <=   writerIndex    <=     size
class Buffer
{
public:
    static const size_t kCheapPrepend = 8;
    static const size_t kInitialSize = 1024;

    explicit Buffer(size_t initialSize = kInitialSize)
        : buffer_(kCheapPrepend + initialSize)
        , readerIndex_(kCheapPrepend)
        , writerIndex_(kCheapPrepend)
    {}

    size_t readableBytes() const 
    {
        return writerIndex_ - readerIndex_;
    }

    size_t writableBytes() const
    {
        return buffer_.size() - writerIndex_;
    }

    size_t prependableBytes() const
    {
        return readerIndex_;
    }

    // 获取缓冲区buffer_中，可读数据的起始地址
    const char* peek() const
    {
        return begin() + readerIndex_;
    }

	// 因buffer_被读取了len字节的数据，故需要通过移动readIndex_来达到释放缓冲区的效果
    void retrieve(size_t len)
    {
        if (len < readableBytes())
        {
            readerIndex_ += len; // 应用只读取了刻度缓冲区数据的一部分，就是len，还剩下readerIndex_ += len -> writerIndex_
        }
        else   // len == readableBytes()
        {
            retrieveAll();
        }
    }
	
	// 因buffer_被全部读取走了，故需要通过移动readIndex_和writerIndex_来达到清空缓冲区的效果
    void retrieveAll()
    {
        readerIndex_ = writerIndex_ = kCheapPrepend;
    }

    // 拿走buffer_中的所有数据，并转换为string返回
    std::string retrieveAllAsString()
    {
        return retrieveAsString(readableBytes()); // 应用可读取数据的长度
    }
	
	// 拿走buffer_中len字节的数据，并转换为string返回
    std::string retrieveAsString(size_t len)
    {
		if (len > readableBytes())
        {
            LOG_FATAL("read %d bytes out of range!\n", len);
        }
		
        std::string result(peek(), len);
        retrieve(len); // 上面一句把缓冲区中可读的数据，已经读取出来，这里肯定要对缓冲区进行复位操作
        return result;
    }

    // 确保buffer_缓冲区可写入len字节的数据
    void ensureWriteableBytes(size_t len)
    {
        if (writableBytes() < len)
        {
			/*
                void makeSpace(size_t len)函数的功能：
                1）如果buffer_缓冲区无法再容纳len字节的数据，需要进行扩容
                2）如果buffer_缓冲区还能再容纳len字节的数据但越界了，则需要进行将数据前移到buffer_.begin()+kCheapPredend的位置
            */ 
            makeSpace(len); 
        }
    }

    // 把[data, data+len]中的数据，添加到writable的buffer_缓冲区当中
    void append(const char *data, size_t len)
    {
		// 确保buffer_中，有len长度的可写空间
        ensureWriteableBytes(len);
        std::copy(data, data+len, beginWrite());
        writerIndex_ += len;
    }

    char* beginWrite()
    {
        return begin() + writerIndex_;
    }

    const char* beginWrite() const
    {
        return begin() + writerIndex_;
    }

    // 从fd上读取数据（从fd的接收缓冲区读取数据）
    ssize_t readFd(int fd, int* saveErrno);
    // 通过fd发送数据（给fd发送缓冲区写入数据）
    ssize_t writeFd(int fd, int* saveErrno);
private:
    char* begin()
    {
        // 获取buffer_中，首个元素的地址，即数组的起始地址
        return &*buffer_.begin();  // vector底层数组首元素的地址，也就是数组的起始地址
    }
    const char* begin() const
    {
        return &*buffer_.begin();
    }
	
	/* 
        buffer_中，剩余可用空间的大小{writableBytes() + prependableBytes() - kCheapPrepend}与len的关系，选择
        1）当“剩余可用空间 < len”，则对buffer_进行扩容
        2）当“剩余可用空间 >= len”，则通过前移[readableIndex_,writableIndex_)范围中的数据到begin()+kCheapPredend位置
    */
    void makeSpace(size_t len)
    {
        if (writableBytes() + prependableBytes() - kCheapPrepend < len)
        {
            buffer_.resize(writerIndex_ + len);
        }
        else
        {
			/* move readable data to the front, make space inside buffer */
            size_t readable = readableBytes();
			// [readableIndex_,writableIndex_) ==> [begin() + kCheapPrepend, ...)
            std::copy(begin() + readerIndex_, begin() + writerIndex_, begin() + kCheapPrepend);
			// readjust the readIndex_ and writeIndex_
            readerIndex_ = kCheapPrepend;
            writerIndex_ = readerIndex_ + readable;
        }
    }

    std::vector<char> buffer_;
    size_t readerIndex_;
    size_t writerIndex_;
};