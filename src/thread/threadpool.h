#ifndef _THREADPOOL_H_
#define _THREADPOOL_H_
#include <stddef.h>

#include "taskexecutor.h"
#include "logger.h"

class ThreadPool : public TaskExecutor {
public:
    enum class Priority {
        Lowest = 0,
        Low,
        Normal,
        High,
        Highest
    };

    ThreadPool(int num = 1, Priority priority = Priority::Highest, 
               bool auto_run = true, bool set_affinity = true, 
               const std::string& pool_name = "thread pool");
    ~ThreadPool();
    
public:
    Task::Ptr async(TaskIn task, bool may_sync = true) override;
    Task::Ptr async_first(TaskIn task, bool may_sync = true) override;
    size_t size();
    static bool setPriority(Priority priority = Priority::Highest, 
                            std::thread::native_handle_type threadId = 0);
    void start();

private:
    void run();
    void wait();
    void shutdown();

private:
    size_t thread_num_;
    Logger::Ptr logger_;
    thread_group thread_group_;
    TaskQueue<Task::Ptr> queue_;
    std::function<void(int)> on_setup_;
}

#endif