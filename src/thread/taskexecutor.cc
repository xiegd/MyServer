#include "taskexecutor.h"

#include <memory>
#include <mutex>
#include "timeticker.h"
#include "utility.h"
#include "eventpoller.h"
#include "threadpool.h"

namespace xkernel {
///////////////////////////// ThreadLoadCounter /////////////////////////////////

ThreadLoadCounter::ThreadLoadCounter(uint64_t max_size, uint64_t max_usec) 
    : max_size_(max_size), max_usec_(max_usec) {
    last_sleep_time_ = last_wake_time_ = TimeUtil::getCurrentMicrosecond();
}

void ThreadLoadCounter::startSleep() {
    std::lock_guard<decltype(mtx_)> lock(mtx_);
    sleeping_ = true;
    auto current_time = TimeUtil::getCurrentMicrosecond();
    auto run_time = current_time - last_wake_time_;
    last_sleep_time_ = current_time;
    time_list_.emplace_back(run_time, false);
    if (time_list_.size() > max_size_) {
        time_list_.pop_front();
    }
}

void ThreadLoadCounter::sleepWakeUp() {
    std::lock_guard<decltype(mtx_)> lock(mtx_);
    sleeping_ = false;
    auto current_time = TimeUtil::getCurrentMicrosecond();
    auto sleep_time = current_time - last_sleep_time_;
    last_wake_time_ = current_time;
    time_list_.emplace_back(sleep_time, true);
    if (time_list_.size() > max_size_) {
        time_list_.pop_front();
    }
}

int ThreadLoadCounter::load() {
    std::lock_guard<decltype(mtx_)> lock(mtx_);
    uint64_t total_sleep_time = 0;
    uint64_t total_run_time = 0;
    time_list_.forEach([&total_sleep_time, &total_run_time](const TimeRecord& rcd) {
        if (rcd.sleep_) {
            total_sleep_time += rcd.time_;
        } else {
            total_run_time += rcd.time_;
        }
    });
    if (sleeping_) {
        total_sleep_time += (TimeUtil::getCurrentMicrosecond() - last_sleep_time_);
    } else {
        total_run_time += (TimeUtil::getCurrentMicrosecond() - last_wake_time_);
    }
    uint64_t total_time = total_sleep_time + total_run_time;
    while ((time_list_.size() != 0) && (total_time > max_usec_ || time_list_.size() > max_size_)) {
        TimeRecord& rcd = time_list_.front();
        if (rcd.sleep_) {
            total_sleep_time -= rcd.time_;
        } else {
            total_run_time -= rcd.time_;
        }
        total_time -= rcd.time_;
        time_list_.pop_front();
    }
    if (total_time == 0) {
        return 0;
    }
    return static_cast<int>(total_run_time * 100 / total_time);
}

//////////////////////// TaskExecutorInterface /////////////////////////////////

Task::Ptr TaskExecutorInterface::asyncFirst(TaskIn task, bool may_sync) {
    return async(std::move(task), may_sync);
}

void TaskExecutorInterface::sync(const TaskIn& task) {
    semaphore sem;
    auto ret = async([&]() {
        onceToken token(nullptr, [&]() { sem.post(); });
        task();
    });
    if (ret && *ret) {
        sem.wait();
    }
}

void TaskExecutorInterface::syncFirst(const TaskIn& task) {
    semaphore sem;
    auto ret = asyncFirst([&]() {
        onceToken token(nullptr, [&]() { sem.post(); });
        task();
    });
    if (ret && *ret) {
        sem.wait();
    }
}

/////////////////////////////// TaskExecutor //////////////////////////////////////

TaskExecutor::TaskExecutor(uint64_t max_size, uint64_t max_usec) 
    : ThreadLoadCounter(max_size, max_usec) {}

//////////////////////// TaskExecutorGetterImpl /////////////////////////////////

TaskExecutor::Ptr TaskExecutorGetterImpl::getExecutor() {
    auto thread_idx = thread_idx_;
    if (thread_idx >= threads_.size()) {
        thread_idx = 0;
    }
    TaskExecutor::Ptr executor_min_load = threads_[thread_idx];
    auto min_load = executor_min_load->load();
    for (size_t i = 0; i < threads_.size(); ++i) {
        ++thread_idx;
        if (thread_idx >= threads_.size()) {
            thread_idx = 0;
        }
        auto executor = threads_[thread_idx];
        auto load = executor->load();
        if (load < min_load) {
            min_load = load;
            executor_min_load = executor;
        }
        if (min_load == 0) {
            break;
        }
    }
    thread_idx_ = thread_idx;
    return executor_min_load;
}

size_t TaskExecutorGetterImpl::getExecutorSize() const { return threads_.size(); }

std::vector<int> TaskExecutorGetterImpl::getExecutorLoad() {
    std::vector<int> vec(threads_.size());
    int i = 0;
    for (auto& executor : threads_) {
        vec[i++] = executor->load();
    }
    return vec;
}

void TaskExecutorGetterImpl::getExecutorDelay(const std::function<void(const std::vector<int>&)>& callback) {
    std::shared_ptr<std::vector<int>> delay_vec = std::make_shared<std::vector<int>>(threads_.size());
    std::shared_ptr<void> finished(nullptr,
                                   [callback, delay_vec](void*) { callback(*delay_vec); });
    int index = 0;
    for (auto& th : threads_) {
        std::shared_ptr<Ticker> delay_ticker = std::make_shared<Ticker>();
        th->async([finished, delay_vec, index, delay_ticker]() {
            (*delay_vec)[index] = static_cast<int>(delay_ticker->elapsedTime());
        }, false);
        ++index;
    }
}

void TaskExecutorGetterImpl::forEach(const std::function<void(const TaskExecutor::Ptr&)>& cb) {
    for (auto& th : threads_) {
        cb(th);
    }
}

size_t TaskExecutorGetterImpl::addPoller(const std::string& name, size_t size, 
    Thread_Priority priority, bool register_thread, bool enable_cpu_affinity) {
        auto cpus = std::thread::hardware_concurrency();
        size = size > 0 ? size : cpus;
        for (size_t i = 0; i < size; ++i) {
            auto full_name = name + "_" + std::to_string(i);
            auto cpu_index = i % cpus;
            EventPoller::Ptr poller(new EventPoller(full_name));
            poller->runLoop(false, register_thread);
            poller->async([cpu_index, full_name, priority, enable_cpu_affinity]() {
                ThreadPool::setPriority(priority);
                ThreadUtil::setThreadName(full_name.data());
                if (enable_cpu_affinity) {
                    ThreadUtil::setThreadAffinity(cpu_index);
                }
            });
            threads_.emplace_back(std::move(poller));
        }
        return size;
    }

}  // namespace xkernel
