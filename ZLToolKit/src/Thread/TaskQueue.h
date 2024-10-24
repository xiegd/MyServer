/*
 * TaskQueue.h
 *
 * 该文件定义了一个基于函数对象的任务队列类 TaskQueue。
 * TaskQueue 是线程安全的，使用信号量控制任务队列中的任务数量。
 * 任务可以被添加到队列的末尾或开头，并由执行线程从队列中获取并执行。
 */

#ifndef TASKQUEUE_H_
#define TASKQUEUE_H_

#include <mutex>
#include "Util/List.h"
#include "semaphore.h"

namespace toolkit {

/**
 * @class TaskQueue
 * @brief 基于函数对象的任务队列类
 * 
 * 该类实现了一个线程安全的任务队列，使用信号量控制任务队列中的任务数量。
 * 任务可以被添加到队列的末尾或开头，并由执行线程从队列中获取并执行。
 * 
 * @tparam T 任务类型
 */
template <typename T>
class TaskQueue {
   public:
    /**
     * @brief 将任务添加到队列末尾
     * 
     * @tparam C 任务类型
     * @param task_func 任务函数对象
     */
    template <typename C>
    void push_task(C &&task_func) {
        {
            std::lock_guard<decltype(_mutex)> lock(_mutex);
            _queue.emplace_back(std::forward<C>(task_func));
        }
        _sem.post();
    }

    /**
     * @brief 将任务添加到队列开头
     * 
     * @tparam C 任务类型
     * @param task_func 任务函数对象
     */
    template <typename C>
    void push_task_first(C &&task_func) {
        {
            std::lock_guard<decltype(_mutex)> lock(_mutex);
            _queue.emplace_front(std::forward<C>(task_func));
        }
        _sem.post();
    }

    /**
     * @brief 清空任务队列
     * 
     * @param n 要清空的任务数量
     */
    void push_exit(size_t n) { _sem.post(n); }

    /**
     * @brief 从队列中获取一个任务
     * 
     * 该方法由执行线程调用，从队列中获取一个任务并执行。
     * 
     * @param tsk 任务对象
     * @return 是否成功获取任务
     */
    bool get_task(T &tsk) {
        _sem.wait();
        std::lock_guard<decltype(_mutex)> lock(_mutex);
        if (_queue.empty()) {
            return false;
        }
        tsk = std::move(_queue.front());
        _queue.pop_front();
        return true;
    }

    /**
     * @brief 获取队列中的任务数量
     * 
     * @return 任务数量
     */
    size_t size() const {
        std::lock_guard<decltype(_mutex)> lock(_mutex);
        return _queue.size();
    }

   private:
    List<T> _queue; ///< 任务队列
    mutable std::mutex _mutex; ///< 互斥锁，保护任务队列
    semaphore _sem; ///< 信号量，控制任务数量
};

} /* namespace toolkit */
#endif /* TASKQUEUE_H_ */
