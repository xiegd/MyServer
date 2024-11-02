#ifndef _THREADPOOL_H_
#define _THREADPOOL_H_
#include <stddef.h>
#include <thread>
#include <unordered_map>
#include <memory>

#include "taskexecutor.h"
#include "logger.h"
#include "msgqueue.h"
#include "eventpoller.h"

namespace xkernel {

enum class Thread_Priority {
    Lowest = 0,
    Low,
    Normal,
    High,
    Highest
};

// 线程组类，管理一组线程
class ThreadGroup {
public:
    ThreadGroup() = default;
    ThreadGroup(const ThreadGroup&) = delete;
    ~ThreadGroup();
    ThreadGroup& operator=(const ThreadGroup&) = delete;

public:
    bool isThisThreadIn();                 // 判断当前线程是否在线程组中
    bool isThreadIn(std::thread* thrd);    // 判断指定线程是否在线程组中
    void removeThread(std::thread* thrd);  // 从线程组中移除指定线程
    void joinAll();                        // 等待所有线程结束
    size_t size();                         // 获取线程组中的线程数量
    // 创建新线程并添加到线程组
    template <typename F>
    std::thread* createThread(F&& threadfunc) {
        auto thread_new = std::make_shared<std::thread>(std::forward<F>(threadfunc));
        thread_id_ = thread_new->get_id();
        threads_[thread_id_] = thread_new;
        return thread_new.get();
    }

private:
    std::thread::id thread_id_;            // 最后创建的线程ID
    std::unordered_map<std::thread::id, std::shared_ptr<std::thread>> threads_;  // 线程ID到线程对象的映射表
};

// 线程池，用于任务的执行
class ThreadPool : public TaskExecutor {
public:
    ThreadPool(int num = 1, Thread_Priority priority = Thread_Priority::Highest, 
               bool auto_run = true, bool set_affinity = true, 
               const std::string& pool_name = "thread pool");
    ~ThreadPool();
    
public:
    Task::Ptr async(TaskIn task, bool may_sync = true) override;
    Task::Ptr asyncFirst(TaskIn task, bool may_sync = true) override;
    size_t size();
    static bool setPriority(Thread_Priority priority = Thread_Priority::Highest, 
                            std::thread::native_handle_type threadId = 0);
    void start();

private:
    void run(size_t index);
    void wait();
    void shutdown();

private:
    size_t thread_num_;
    Logger::Ptr logger_;
    ThreadGroup thread_group_;
    MsgQueue<Task::Ptr> queue_;
    std::function<void(int)> on_setup_;
};

// 工作线程池，单例模式，用于事件的轮询
class WorkThreadPool : public std::enable_shared_from_this<WorkThreadPool>,
                       public TaskExecutorGetterImpl {
public:
    using Ptr = std::shared_ptr<WorkThreadPool>;
    ~WorkThreadPool() override = default;
    static WorkThreadPool& Instance();

public:
    static void setPoolSize(size_t size = 0);
    static void enableCpuAffinity(bool enable);
    EventPoller::Ptr getFirstPoller();
    EventPoller::Ptr getPoller();

protected:
    WorkThreadPool();
};

}  // namespace xkernel

#endif