/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef THREADPOOL_H_
#define THREADPOOL_H_

#include "TaskExecutor.h"
#include "TaskQueue.h"
#include "Util/logger.h"
#include "Util/util.h"
#include "threadgroup.h"

namespace toolkit {

/**
 * @class ThreadPool
 * @brief 线程池类,继承自TaskExecutor
 * 
 * 实现了一个可配置的线程池,支持异步任务执行和优先级设置
 */
class ThreadPool : public TaskExecutor {
   public:
    /**
     * @enum Priority
     * @brief 线程优先级枚举
     */
    enum Priority {
        PRIORITY_LOWEST = 0,   ///< 最低优先级
        PRIORITY_LOW,          ///< 低优先级
        PRIORITY_NORMAL,       ///< 普通优先级
        PRIORITY_HIGH,         ///< 高优先级
        PRIORITY_HIGHEST       ///< 最高优先级
    };

    /**
     * @brief 构造函数
     * @param num 线程数量,默认为1
     * @param priority 线程优先级,默认为最高
     * @param auto_run 是否自动启动线程池,默认为true
     * @param set_affinity 是否设置线程亲和性,默认为true
     * @param pool_name 线程池名称,默认为"thread pool"
     */
    ThreadPool(int num = 1, Priority priority = PRIORITY_HIGHEST,
               bool auto_run = true, bool set_affinity = true,
               const std::string &pool_name = "thread pool") {
        _thread_num = num;
        // 设置线程初始化函数
        _on_setup = [pool_name, priority, set_affinity](int index) {
            std::string name = pool_name + ' ' + std::to_string(index);
            setPriority(priority);
            setThreadName(name.data());
            if (set_affinity) {
                setThreadAffinity(index % std::thread::hardware_concurrency());
            }
        };
        // 对于继承自enable_shared_from_this的类，需要使用shared_from_this()来获取shared_ptr
        // 避免多个独立的shared_ptr指向同一个对象，导致无法析构, 
        //在异步操作中确保对象的生命周期
        _logger = Logger::Instance().shared_from_this();
        if (auto_run) {
            start();
        }
    }

    /**
     * @brief 析构函数
     * 
     * 关闭线程池并等待所有线程结束
     */
    ~ThreadPool() {
        shutdown();
        wait();
    }

    /**
     * @brief 异步执行任务
     * @param task 要执行的任务
     * @param may_sync 是否允许同步执行,默认为true
     * @return 任务的智能指针
     */
    Task::Ptr async(TaskIn task, bool may_sync = true) override {
        if (may_sync && _thread_group.is_this_thread_in()) {
            task();
            return nullptr;
        }
        auto ret = std::make_shared<Task>(std::move(task));
        _queue.push_task(ret);
        return ret;
    }

    /**
     * @brief 以最高优先级异步执行任务
     * @param task 要执行的任务
     * @param may_sync 是否允许同步执行,默认为true
     * @return 任务的智能指针
     */
    Task::Ptr async_first(TaskIn task, bool may_sync = true) override {
        // 如果可以同步执行，且在线程组中，则同步执行
        if (may_sync && _thread_group.is_this_thread_in()) {
            task();
            return nullptr;
        }

        auto ret = std::make_shared<Task>(std::move(task));
        _queue.push_task_first(ret);
        return ret;
    }

    /**
     * @brief 获取当前任务队列大小
     * @return 任务队列中的任务数量
     */
    size_t size() { return _queue.size(); }

    /**
     * @brief 设置线程优先级
     * @param priority 要设置的优先级
     * @param threadId 线程ID,默认为0(当前线程)
     * @return 是否设置成功
     */
    static bool setPriority(Priority priority = PRIORITY_NORMAL,
                            std::thread::native_handle_type threadId = 0) {
        // 根据不同平台设置线程优先级
#if defined(_WIN32)
        // Windows平台实现
        static int Priorities[] = {
            THREAD_PRIORITY_LOWEST, THREAD_PRIORITY_BELOW_NORMAL,
            THREAD_PRIORITY_NORMAL, THREAD_PRIORITY_ABOVE_NORMAL,
            THREAD_PRIORITY_HIGHEST};
        if (priority != PRIORITY_NORMAL &&
            SetThreadPriority(GetCurrentThread(), Priorities[priority]) == 0) {
            return false;
        }
        return true;
#else
        // 非Windows平台实现(如Linux)
        // 获取SCHED_FIFO调度策略的最小优先级
        static int Min = sched_get_priority_min(SCHED_FIFO);
        if (Min == -1) {
            return false;
        }
        // 获取SCHED_FIFO调度策略的最大优先级
        static int Max = sched_get_priority_max(SCHED_FIFO);
        if (Max == -1) {
            return false;
        }
        static int Priorities[] = {Min, Min + (Max - Min) / 4,
                                   Min + (Max - Min) / 2,
                                   Min + (Max - Min) * 3 / 4, Max};

        if (threadId == 0) {
            threadId = pthread_self();  // 如果没有指定线程ID，则使用当前线程
        }
        struct sched_param params;  // 创建调度参数结构体
        params.sched_priority = Priorities[priority];  // 填充结构体
        return pthread_setschedparam(threadId, SCHED_FIFO, &params) == 0;  // 设置线程优先级
#endif
    }

    /**
     * @brief 启动线程池
     */
    void start() {
        if (_thread_num <= 0) {
            return;
        }
        size_t total = _thread_num - _thread_group.size();
        for (size_t i = 0; i < total; ++i) {
            _thread_group.create_thread([this, i]() { run(i); });
        }
    }

   private:
    /**
     * @brief 线程运行函数
     * @param index 线程索引
     */
    void run(size_t index) {
        _on_setup(index);
        Task::Ptr task;
        while (true) {
            startSleep();
            if (!_queue.get_task(task)) {
                // 空任务,退出线程
                break;
            }
            sleepWakeUp();
            try {
                (*task)();
                task = nullptr;
            } catch (std::exception &ex) {
                ErrorL << "ThreadPool catch a exception: " << ex.what();
            }
        }
    }

    /**
     * @brief 等待所有线程结束
     */
    void wait() { _thread_group.join_all(); }

    /**
     * @brief 关闭线程池
     */
    void shutdown() { _queue.push_exit(_thread_num); }

   private:
    size_t _thread_num;                    ///< 线程数量
    Logger::Ptr _logger;                   ///< 日志指针
    thread_group _thread_group;            ///< 线程组
    TaskQueue<Task::Ptr> _queue;           ///< 任务队列
    std::function<void(int)> _on_setup;    ///< 线程初始化函数
};

} /* namespace toolkit */
#endif /* THREADPOOL_H_ */
