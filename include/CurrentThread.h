#pragma once

#include <unistd.h>
#include <sys/syscall.h>

namespace CurrentThread
{
    extern __thread int t_cachedTid;

    void cacheTid();

    inline int tid()
    {
        if (__builtin_expect(t_cachedTid == 0, 0))
        {
	    // 通过linux系统调用SYS_gettid()，获取当前线程的tid值
            cacheTid();
        }
        return t_cachedTid;
    }
}
