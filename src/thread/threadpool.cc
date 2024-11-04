#include "threadpool.h"

#include <stdexcept>
#include "logger.h"
#include "msgqueue.h"
#include "taskexecutor.h"
#include "utility.h"


namespace xkernel {

//////////////////////////////////// ThreadGroup //////////////////////////////////////

ThreadGroup::~ThreadGroup() { threads_.clear();}

bool ThreadGroup::isThisThreadIn() {
    auto thread_id = std::this_thread::get_id();
    if (thread_id == thread_id_) {
        return true;  // 如果是最后创建的线程
    }
    return threads_.find(thread_id) != threads_.end();
}

bool ThreadGroup::isThreadIn(std::thread* thrd) {
    if (!thrd) {
        return false;
    }
    return threads_.find(thrd->get_id()) != threads_.end();
}

void ThreadGroup::removeThread(std::thread* thrd) {
    auto it = threads_.find(thrd->get_id());
    if (it != threads_.end()) {
        threads_.erase(it);
    }
}

void ThreadGroup::joinAll() {
    if (isThisThreadIn()) {
        throw std::runtime_error("Trying joining itself in thread_group");
    }
    for (auto& it : threads_) {
        if (it.second->joinable()) {
            it.second->join();
        }
    }
    threads_.clear();
}

size_t ThreadGroup::size() { return threads_.size(); }

//////////////////////////////////// ThreadPool //////////////////////////////////////

ThreadPool::ThreadPool(int num, Thread_Priority priority, bool auto_run, 
                       bool set_affinity, const std::string& pool_name) {
    thread_num_ = num;
    on_setup_ = [pool_name, priority, set_affinity](int index) {
        std::string name = pool_name + " " + std::to_string(index);
        setPriority(priority);
        ThreadUtil::setThreadName(name.data());
        if (set_affinity) {
            ThreadUtil::setThreadAffinity(index % std::thread::hardware_concurrency());
        }
    };
    logger_ = Logger::Instance().shared_from_this();
    if (auto_run) {
        start();
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
    wait();
}
    
Task::Ptr ThreadPool::async(TaskIn task, bool may_sync) {
    if (may_sync && thread_group_.isThisThreadIn()) {
        task();
        return nullptr;
    }
    auto ret = std::make_shared<Task>(std::move(task));
    queue_.putMsg(ret);
    return ret;
}

Task::Ptr ThreadPool::asyncFirst(TaskIn task, bool may_sync) {
    if (may_sync && thread_group_.isThisThreadIn()) {
        task();
        return nullptr;
    }
    auto ret = std::make_shared<Task>(std::move(task));
    queue_.putMsgToHead(ret);
    return ret;
}

size_t ThreadPool::size() { return queue_.size(); }

bool ThreadPool::setPriority(Thread_Priority priority, std::thread::native_handle_type threadId) {
    static int Min = sched_get_priority_min(SCHED_FIFO);  // 获取SCHED_FIFO调度策略的最小优先级
    if (Min == -1) {
        return false;
    }
    static int Max = sched_get_priority_max(SCHED_FIFO);  // 获取SCHED_FIFO调度策略的最大优先级
    if (Max == -1 ) {
        return false;
    }
    static int Priorities[] {Min, Min + (Max - Min)/4,
                            Min + (Max - Min)/2,
                            Min + (Max - Min) * 3/4, Max};
    if (threadId == 0) {
        threadId = pthread_self();
    }
    struct sched_param params;  // 创建调度参数结构体
    params.sched_priority = Priorities[static_cast<int>(priority)];
    return pthread_setschedparam(threadId, SCHED_FIFO, &params) == 0;
}

void ThreadPool::start() {
    if (thread_num_ <= 0) {
        return ;
    }
    size_t total = thread_num_ - thread_group_.size();
    for (size_t i = 0; i < total; ++i) {
        thread_group_.createThread([this, i](){ run(i); });
    }
}

void ThreadPool::run(size_t index) {
    on_setup_(index);
    Task::Ptr task;
    while (true) {
        startSleep();
        if (!queue_.getMsg(task)) {
            break;  // 空任务，退出线程
        }
        sleepWakeUp();
        try {
            (*task)();
            task = nullptr;
        } catch (std::exception& ex) {
            ErrorL << "ThreadPool catch a exception: " << ex.what();
        }
    }
}

void ThreadPool::wait() { thread_group_.joinAll(); }
void ThreadPool::shutdown() { queue_.pushExit(thread_num_); }

//////////////////////////////////// WorkThreadPool //////////////////////////////////////

static size_t s_pool_size = 0;
static bool s_enable_cpu_affinity = true;

INSTANCE_IMP(WorkThreadPool)

// 设置线程池大小, 必须在WorkThreadPool单例创建前调用
void WorkThreadPool::setPoolSize(size_t size) { s_pool_size = size; }

void WorkThreadPool::enableCpuAffinity(bool enable) { s_enable_cpu_affinity = enable; }

EventPoller::Ptr WorkThreadPool::getPoller() {
    return std::static_pointer_cast<EventPoller>(getExecutor());
}

EventPoller::Ptr WorkThreadPool::getFirstPoller() {
    return std::static_pointer_cast<EventPoller>(threads_.front());
}

WorkThreadPool::WorkThreadPool() {
    addPoller("work poller", s_pool_size, Thread_Priority::Lowest, false, s_enable_cpu_affinity);
}

}  // namespace xkernel
