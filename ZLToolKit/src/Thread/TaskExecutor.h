/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef ZLTOOLKIT_TASKEXECUTOR_H
#define ZLTOOLKIT_TASKEXECUTOR_H

#include <functional>
#include <memory>
#include <mutex>

#include "Util/List.h"
#include "Util/util.h"

namespace toolkit {

/**
 * @class ThreadLoadCounter
 * @brief CPU负载计算器
 * 
 * 用于计算和跟踪线程的CPU使用率。
 */
class ThreadLoadCounter {
public:
    /**
     * @brief 构造函数
     * @param max_size 统计样本数量
     * @param max_usec 统计时间窗口大小（微秒）
     */
    ThreadLoadCounter(uint64_t max_size, uint64_t max_usec);
    ~ThreadLoadCounter() = default;

    /**
     * @brief 线程进入休眠状态
     */
    void startSleep();

    /**
     * @brief 线程从休眠状态唤醒
     */
    void sleepWakeUp();

    /**
     * @brief 获取当前线程的CPU使用率
     * @return CPU使用率，范围为0~100
     */
    int load();

private:
    // 内部使用的时间记录结构
    struct TimeRecord {
        TimeRecord(uint64_t tm, bool slp) : _time(tm), _sleep(slp) {}
        bool _sleep;
        uint64_t _time;
    };

private:
    bool _sleeping = true;
    uint64_t _last_sleep_time;
    uint64_t _last_wake_time;
    uint64_t _max_size;  // 统计样本数量, 只统计最新的_max_size条记录
    uint64_t _max_usec;  // 统计总时长，只统计距离现在<=_max_usec的这段时间
    std::mutex _mtx;
    List<TimeRecord> _time_list;
};

/**
 * @class TaskCancelable
 * @brief 可取消任务的基类
 */
class TaskCancelable : public noncopyable {
public:
    TaskCancelable() = default;
    virtual ~TaskCancelable() = default;
    
    /**
     * @brief 取消任务
     */
    virtual void cancel() = 0;
};

/**
 * @class TaskCancelableImp
 * @brief 可取消任务的实现类
 * 
 * @tparam R 任务返回值类型
 * @tparam ArgTypes 任务参数类型
 */
template <class R, class... ArgTypes>
class TaskCancelableImp;

template <class R, class... ArgTypes>
class TaskCancelableImp<R(ArgTypes...)> : public TaskCancelable {
public:
    using Ptr = std::shared_ptr<TaskCancelableImp>;
    using func_type = std::function<R(ArgTypes...)>;

    ~TaskCancelableImp() = default;

    /**
     * @brief 构造函数
     * @param task 要执行的任务
     */
    template <typename FUNC>
    TaskCancelableImp(FUNC &&task) {
        _strongTask = std::make_shared<func_type>(std::forward<FUNC>(task));
        _weakTask = _strongTask;  // 弱引用避免循环引用 
    }

    /**
     * @brief 取消任务
     */
    void cancel() override { 
        _strongTask = nullptr; // 引用计数减1
    }

    /**
     * @brief 检查任务是否有效
     */
    operator bool() { return _strongTask && *_strongTask; }

    /**
     * @brief 将任务设置为空
     */
    void operator=(std::nullptr_t) { _strongTask = nullptr; }

    /**
     * @brief 执行任务
     * @param args 任务参数
     * @return 任务执行结果
     */
    R operator()(ArgTypes... args) const {
        auto strongTask = _weakTask.lock();
        if (strongTask && *strongTask) {
            return (*strongTask)(std::forward<ArgTypes>(args)...);
        }
        return defaultValue<R>();
    }

    /**
     * @brief 获取void类型的默认值
     */
    template <typename T>
    static typename std::enable_if<std::is_void<T>::value, void>::type
    defaultValue() {}

    /**
     * @brief 获取指针类型的默认值
     */
    template <typename T>
    static typename std::enable_if<std::is_pointer<T>::value, T>::type
    defaultValue() {
        return nullptr;
    }

    /**
     * @brief 获取整型的默认值
     */
    template <typename T>
    static typename std::enable_if<std::is_integral<T>::value, T>::type
    defaultValue() {
        return 0;
    }

protected:
    std::weak_ptr<func_type> _weakTask;
    std::shared_ptr<func_type> _strongTask;
};

using TaskIn = std::function<void()>;  // 通用函数包装器，用于表示一个不接受参数且不返回任何值的函数
using Task = TaskCancelableImp<void()>;  // 模板类实例，表示一个不接受参数且不返回任何值的可取消任务

/**
 * @class TaskExecutorInterface
 * @brief 任务执行器接口
 * 是一个虚基类
 */
class TaskExecutorInterface {
public:
    TaskExecutorInterface() = default;
    virtual ~TaskExecutorInterface() = default;

    /**
     * @brief 异步执行任务
     * @param task 要执行的任务
     * @param may_sync 是否允许同步执行
     * @return 任务是否添加成功
     * 纯虚函数在EvnetPoller里实现
     */
    virtual Task::Ptr async(TaskIn task, bool may_sync = true) = 0;  // 纯虚函数，抽象类

    /**
     * @brief 以最高优先级异步执行任务
     * @param task 要执行的任务
     * @param may_sync 是否允许同步执行
     * @return 任务是否添加成功
     */
    virtual Task::Ptr async_first(TaskIn task, bool may_sync = true);

    /**
     * @brief 同步执行任务
     * @param task 要执行的任务
     */
    void sync(const TaskIn &task);

    /**
     * @brief 以最高优先级同步执行任务
     * @param task 要执行的任务
     */
    void sync_first(const TaskIn &task);
};

/**
 * @class TaskExecutor
 * @brief 任务执行器
 */
class TaskExecutor : public ThreadLoadCounter, public TaskExecutorInterface {
public:
    using Ptr = std::shared_ptr<TaskExecutor>;

    /**
     * @brief 构造函数
     * @param max_size CPU负载统计样本数
     * @param max_usec CPU负载统计时间窗口大小
     */
    TaskExecutor(uint64_t max_size = 32, uint64_t max_usec = 2 * 1000 * 1000);
    ~TaskExecutor() = default;
};

/**
 * @class TaskExecutorGetter
 * @brief 任务执行器获取接口
 */
class TaskExecutorGetter {
public:
    using Ptr = std::shared_ptr<TaskExecutorGetter>;

    virtual ~TaskExecutorGetter() = default;

    /**
     * @brief 获取任务执行器
     * @return 任务执行器指针
     */
    virtual TaskExecutor::Ptr getExecutor() = 0;

    /**
     * @brief 获取执行器数量
     * @return 执行器数量
     */
    virtual size_t getExecutorSize() const = 0;
};

/**
 * @class TaskExecutorGetterImp
 * @brief 任务执行器获取接口的实现类
 */
class TaskExecutorGetterImp : public TaskExecutorGetter {
public:
    TaskExecutorGetterImp() = default;
    ~TaskExecutorGetterImp() = default;

    /**
     * @brief 获取最空闲的任务执行器
     * @return 任务执行器指针
     * 即获取cpu使用率最小的执行器
     */
    TaskExecutor::Ptr getExecutor() override;

    /**
     * @brief 获取所有线程的负载率
     * @return 负载率列表
     */
    std::vector<int> getExecutorLoad();

    /**
     * @brief 获取所有线程任务执行延时
     * @param callback 回调函数，用于接收延时数据
     */
    void getExecutorDelay(
        const std::function<void(const std::vector<int> &)> &callback);

    /**
     * @brief 遍历所有线程
     * @param cb 回调函数，用于处理每个执行器
     */
    void for_each(const std::function<void(const TaskExecutor::Ptr &)> &cb);

    /**
     * @brief 获取线程数
     * @return 线程数
     */
    size_t getExecutorSize() const override;

protected:
    /**
     * @brief 添加一个轮询器
     * @param name 轮询器名称
     * @param size 线程池大小
     * @param priority 优先级
     * @param register_thread 是否注册线程
     * @param enable_cpu_affinity 是否启用CPU亲和性
     * @return 添加的轮询器数量
     */
    size_t addPoller(const std::string &name, size_t size, int priority,
                     bool register_thread, bool enable_cpu_affinity = true);

protected:
    size_t _thread_pos = 0;  // 跟踪当前选择的线程(TaskExector)索引，是上一次选出的负载最小的线程
    std::vector<TaskExecutor::Ptr> _threads;  // event poller pool
};

}  // namespace toolkit
#endif  // ZLTOOLKIT_TASKEXECUTOR_H
