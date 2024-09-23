#ifndef _TASKEXCUTOR_H_
#define _TASKEXCUTOR_H_

#include <functional>
#include <memory>
#include <mutex>
#include "utility.h"

class ThreadLoadCounter {
public:
    ThreadLoadCounter(uint64_t max_size, uint64_t max_usec);
    ~ThreadLoadCounter() = default;

public:
    void startSleep();
    void sleepWakeUp();
    int load();  // 获取当前线程的CPU使用率

private:
    struct TimeRecord {
        TimeRecord(uint64_t tm, bool slp) : time_(tm), sleep_(slp) {}
        bool sleep_;
        uint64_t time_;
    }
    bool sleeping_ = true;
    uint64_t last_sleep_time_;  // 上一次休眠时间
    uint64_t last_wake_time_;  // 上一次唤醒时间
    uint64_t max_size_;  // 统计样本数量
    uint64_t max_usec_;  // 统计时间窗口大小
    std::mutex mtx_;
    List<TimeRecord> time_list_;
};

// 可取消任务的抽象基类
class TaskCacelable : public Noncopyable {
public:
    TaskCacelable() = default;
    virtual ~TaskCacelable() = default;

    virtual void cancel() = 0;
};

template<typename R, typename... ArgTypes>
class TaskCancelableImpl;
 
template<typename R, typename... ArgTypes>
// 类的偏特化, 表示返回值为R, 参数列表为ArgTypes...的函数
// 即适用于所有函数类型的偏特化
class TaskCancelableImpl<R(ArgTypes...)> : public TaskCacelable {
public:
    using Ptr = std::shared_ptr<TaskCancelableImpl>;
    using func_type = std::function<R(ArgTypes...)>;

    template<typename FUNC> 
    TaskCancelableImpl(FUNC&& task) {
        strongTask_ = std::make_shared<func_type>(std::forward<FUNC>(task));
        weakTask_ = strongTask_;
    }

    ~TaskCancelableImpl() = default;

public:
    void cancel() override {
        strongTask_ = nullptr; // 引用计数减1
    }
    
protected:
    std::weak_ptr<func_type> weakTask_;
    std::shared_ptr<func_type> strongTask_;
};

using TaskIn = std::function<void()>;
using Task = TaskCancelableImpl<void()>;

// 任务执行器接口
class TaskExecutorInterface {
public:
    TaskExecutorInterface() = default;
    virtual ~TaskExecutorInterface() = default;

public:
    virtual Task::Ptr async(TaskIn task, bool may_sync = true) = 0;  // 异步执行
    virtual Task::Ptr async_first(Task task, bool may_sync = true);  // 以最高优先级异步执行
    void sync(const TaskIn& Task);  // 同步执行
    void sync_first(const TaskIn& Task);  // 以最高优先级同步执行
};

class TaskExecutor : public ThreadLoadCounter, public TaskExecutorInterface {
public: 
    using Ptr = std::shared_ptr<TaskExecutor>;

    TaskExecutor(uint64_t max_size = 32, uint64_t max_usec = 2 * 1000 * 1000);
    ~TaskExecutor() = default;
}

// 任务执行器获取接口
class TaskExecutorGetter {
public:
    using Ptr = std::shared_ptr<TaskExecutorGetter>;

    TaskExecutorGetter() = default;
    ~TaskExecutorGetter() = default;
public:
    virtual TaskExector::Ptr getExecutor() = 0;
    virtual size_t getExecutorSize() const = 0;
}

// 任务执行器获取接口的实现类
class TaskExecutorGetterImpl : public TaskExecutorGetter {
public: 
    TaskExecutorGetterImpl() = default;
    ~TaskExecutorGetterImpl() = default;

public:
    TaskExector::Ptr getExecutor() override;  // 获取最空闲的任务执行器
    size_t getExecutorSize() const override;  // 获取执行器(线程)数量
    std::vector<int> getExecutorLoad();  // 获取所有线程的负载率    
    void getExecutorDelay(const std::function<void(const std::vector<int>&)& callback);  // 获取所有线程任务执行时延
    void forEach(const std::function<void(const TaskExector::Ptr&)>& callback);  // 遍历所有线程

protected:
    size_t addPoller(const std::string& name, size_t size, int priority,
        bool register_thread, bool enable_cpu_affinity = true);
protected:
    size_t thread_idx_ = 0;  // 跟踪当前选择的线程(TaskExector)索引
    std::vector<TaskExector::Ptr> threads_;
};
#endif