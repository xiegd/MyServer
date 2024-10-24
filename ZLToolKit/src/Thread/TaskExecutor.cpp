/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "TaskExecutor.h"

#include <memory>

#include "Poller/EventPoller.h"
#include "Util/TimeTicker.h"
#include "Util/onceToken.h"

using namespace std;

namespace toolkit {

ThreadLoadCounter::ThreadLoadCounter(uint64_t max_size, uint64_t max_usec) {
    _last_sleep_time = _last_wake_time = getCurrentMicrosecond();  // 初始化休眠和唤醒时间为当前时间
    _max_size = max_size;
    _max_usec = max_usec;
}

// 线程进入休眠状态
void ThreadLoadCounter::startSleep() {
    lock_guard<mutex> lck(_mtx);
    _sleeping = true;
    auto current_time = getCurrentMicrosecond();
    auto run_time = current_time - _last_wake_time;
    _last_sleep_time = current_time;
    _time_list.emplace_back(run_time, false);
    if (_time_list.size() > _max_size) {
        _time_list.pop_front();
    }
}

void ThreadLoadCounter::sleepWakeUp() {
    lock_guard<mutex> lck(_mtx);
    _sleeping = false;
    auto current_time = getCurrentMicrosecond();
    auto sleep_time = current_time - _last_sleep_time;
    _last_wake_time = current_time;
    _time_list.emplace_back(sleep_time, true);
    if (_time_list.size() > _max_size) {
        _time_list.pop_front();
    }
}

int ThreadLoadCounter::load() {
    lock_guard<mutex> lck(_mtx);
    uint64_t totalSleepTime = 0;
    uint64_t totalRunTime = 0;
    _time_list.for_each([&](const TimeRecord &rcd) {
        if (rcd._sleep) {
            totalSleepTime += rcd._time;
        } else {
            totalRunTime += rcd._time;
        }
    });
    // 加上上次记录到现在的时间
    if (_sleeping) {
        totalSleepTime += (getCurrentMicrosecond() - _last_sleep_time);
    } else {
        totalRunTime += (getCurrentMicrosecond() - _last_wake_time);
    }

    uint64_t totalTime = totalRunTime + totalSleepTime;
    // 丢弃超出max_size_和max_usec_的记录
    while ((_time_list.size() != 0) &&
           (totalTime > _max_usec || _time_list.size() > _max_size)) {
        TimeRecord &rcd = _time_list.front();
        if (rcd._sleep) {
            totalSleepTime -= rcd._time;
        } else {
            totalRunTime -= rcd._time;
        }
        totalTime -= rcd._time;
        _time_list.pop_front();
    }
    if (totalTime == 0) {
        return 0;
    }
    return (int)(totalRunTime * 100 / totalTime);
}

////////////////////////////////////////////////////////////////////////////

Task::Ptr TaskExecutorInterface::async_first(TaskIn task, bool may_sync) {
    return async(std::move(task), may_sync);
}

void TaskExecutorInterface::sync(const TaskIn &task) {
    semaphore sem;
    // 异步执行task, onceToken在lambda函数执行完后析构
    auto ret = async([&]() {
        onceToken token(nullptr, [&]() {
            // 确保无论task()是否抛异常，都会执行sem.post()
            sem.post();
        });
        task();  // 执行任务
    });
    if (ret && *ret) {
        sem.wait();  // 等待task执行完毕
    }
}

void TaskExecutorInterface::sync_first(const TaskIn &task) {
    semaphore sem;
    auto ret = async_first([&]() {
        onceToken token(nullptr, [&]() {
            // 确保无论task()是否抛异常，都会执行sem.post()
            sem.post();
        });
        task();
    });
    if (ret && *ret) {
        sem.wait();
    }
}

//////////////////////////////////////////////////////////////////

TaskExecutor::TaskExecutor(uint64_t max_size, uint64_t max_usec)
    : ThreadLoadCounter(max_size, max_usec) {}

//////////////////////////////////////////////////////////////////

TaskExecutor::Ptr TaskExecutorGetterImp::getExecutor() {
    auto thread_pos = _thread_pos;
    if (thread_pos >= _threads.size()) {
        thread_pos = 0;
    }
    // 获取当前选择线程的cpu使用率
    TaskExecutor::Ptr executor_min_load = _threads[thread_pos];
    auto min_load = executor_min_load->load();
    // 遍历所有线程，获取cpu使用率最小的线程
    // 从thread_pos遍历，避免从头开始遍历在负载相同时
    // 总是选择第一个线程，导致分配不均，同时对cpu缓冲更友好
    for (size_t i = 0; i < _threads.size(); ++i) {
        ++thread_pos;
        if (thread_pos >= _threads.size()) {
            thread_pos = 0;
        }

        auto th = _threads[thread_pos];
        auto load = th->load();

        if (load < min_load) {
            min_load = load;
            executor_min_load = th;
        }
        if (min_load == 0) {
            break;
        }
    }
    _thread_pos = thread_pos;
    return executor_min_load;
}

vector<int> TaskExecutorGetterImp::getExecutorLoad() {
    vector<int> vec(_threads.size());
    int i = 0;
    for (auto &executor : _threads) {
        vec[i++] = executor->load();
    }
    return vec;
}

void TaskExecutorGetterImp::getExecutorDelay(
    const function<void(const vector<int> &)> &callback) {
    std::shared_ptr<vector<int>> delay_vec =
        std::make_shared<vector<int>>(_threads.size());
    // 创建一个shared_ptr，这里自定义了deleter，也就是这个lambda表达式，
    // 当析构时调用回调函数(deleter) , 把获得的所有线程时延数据传递给回调函数的参数
    shared_ptr<void> finished(nullptr, [callback, delay_vec](void *) {
        //此析构回调触发时，说明已执行完毕所有async任务
        callback((*delay_vec));
    });
    int index = 0;
    for (auto &th : _threads) {
        // 为线程创建计时器
        std::shared_ptr<Ticker> delay_ticker = std::make_shared<Ticker>();
        // 把finished作为参数传递给异步任务，当所有任务完成后, 
        // 在shared_ptr的计数减少到0时，会调用deleter，也就是回调函数
        th->async(
            [finished, delay_vec, index, delay_ticker]() {
                (*delay_vec)[index] = (int)delay_ticker->elapsedTime();
            },
            false);
        ++index;
    }
}

void TaskExecutorGetterImp::for_each(
    const function<void(const TaskExecutor::Ptr &)> &cb) {
    for (auto &th : _threads) {
        cb(th);
    }
}

size_t TaskExecutorGetterImp::getExecutorSize() const {
    return _threads.size();
}
/*
 * 添加一组Poller线程
 * name: 线程名
 * size: 线程数
 * priority: 线程优先级
 * register_thread: 是否注册线程
 * enable_cpu_affinity: 是否启用cpu亲和性
 */
size_t TaskExecutorGetterImp::addPoller(const string &name, size_t size,
                                        int priority, bool register_thread,
                                        bool enable_cpu_affinity) {
    auto cpus = thread::hardware_concurrency();  // 返回支持的并发线程数
    size = size > 0 ? size : cpus;
    for (size_t i = 0; i < size; ++i) {
        auto full_name = name + " " + to_string(i);
        auto cpu_index = i % cpus;
        EventPoller::Ptr poller(new EventPoller(full_name));
        poller->runLoop(false, register_thread);
        poller->async([cpu_index, full_name, priority, enable_cpu_affinity]() {
            // 设置线程优先级
            ThreadPool::setPriority((ThreadPool::Priority)priority);
            // 设置线程名
            setThreadName(full_name.data());
            // 设置cpu亲和性
            if (enable_cpu_affinity) {
                setThreadAffinity(cpu_index);
            }
        });
        _threads.emplace_back(std::move(poller));
    }
    return size;
}

}  // namespace toolkit
