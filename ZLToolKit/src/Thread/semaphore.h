/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

/**
 * @file semaphore.h
 * @brief 实现了一个跨平台的信号量类
 * 
 * 这个文件定义了一个 semaphore 类，它提供了一个可以在多线程环境中使用的计数信号量。
 * 该实现可以在不同的平台上工作，包括那些不支持原生信号量的平台。
 */

#ifndef SEMAPHORE_H_
#define SEMAPHORE_H_

/*
 * 注释掉的代码说明：
 * 目前发现信号量在32位的系统上有问题，
 * 休眠的线程无法被正常唤醒，先禁用之
#if defined(__linux__)
#include <semaphore.h>
#define HAVE_SEM
#endif //HAVE_SEM
*/

#include <condition_variable>
#include <mutex>

namespace toolkit {

/**
 * @class semaphore
 * @brief 实现了一个计数信号量
 * 
 * 这个类提供了一个可以在多线程环境中使用的计数信号量。
 * 它使用 C++11 的条件变量和互斥锁来实现，以确保跨平台兼容性。
 * 如果定义了HAVE_SEM, 则使用原生信号量，否则使用条件变量和互斥锁实现。
 */
class semaphore {
   public:
    /**
     * @brief 构造函数
     * @param initial 信号量的初始值，默认为0
     */
    explicit semaphore(size_t initial = 0) {
#if defined(HAVE_SEM)
        sem_init(&_sem, 0, initial);
#else
        _count = 0;
#endif
    }

    /**
     * @brief 析构函数
     */
    ~semaphore() {
#if defined(HAVE_SEM)
        sem_destroy(&_sem);
#endif
    }

    /**
     * @brief 增加信号量的值
     * @param n 要增加的数量，默认为1
     * 
     * 这个方法会增加信号量的值，并且可能会唤醒等待的线程。
     */
    void post(size_t n = 1) {
#if defined(HAVE_SEM)
        while (n--) {
            sem_post(&_sem);
        }
#else
        std::unique_lock<std::recursive_mutex> lock(_mutex);
        _count += n;
        if (n == 1) {
            _condition.notify_one();
        } else {
            _condition.notify_all();
        }
#endif
    }

    /**
     * @brief 等待并减少信号量的值
     * 
     * 这个方法会等待直到信号量的值大于0，然后将其减1。
     * 如果信号量的值已经大于0，它会立即减1并返回。
     */
    void wait() {
#if defined(HAVE_SEM)
        sem_wait(&_sem);
#else
        std::unique_lock<std::recursive_mutex> lock(_mutex);
        while (_count == 0) {
            _condition.wait(lock);
        }
        --_count;
#endif
    }

   private:
#if defined(HAVE_SEM)
    sem_t _sem;  ///< 原生信号量（当 HAVE_SEM 定义时使用）
#else
    size_t _count;  ///< 信号量的当前值
    std::recursive_mutex _mutex;  ///< 用于保护 _count 的互斥锁
    std::condition_variable_any _condition;  ///< 用于线程等待的条件变量
#endif
};

} /* namespace toolkit */
#endif /* SEMAPHORE_H_ */
