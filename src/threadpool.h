#ifndef _THREADPOOL_H_
#define _THREADPOOL_H_
#include <stddef.h>

class ThreadPool {
public:
    enum class Priority {
        Lowest = 0,
        Low,
        Normal,
        High,
        Highest
    };

    ThreadPool();
    ~ThreadPool();
    
}

#endif