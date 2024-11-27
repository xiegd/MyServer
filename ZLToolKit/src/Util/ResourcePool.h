/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef UTIL_RECYCLEPOOL_H_
#define UTIL_RECYCLEPOOL_H_

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_set>

#include "List.h"

namespace toolkit {

// 检查编译器是否支持可变参数模板
// 这里检查 GCC 版本 >= 4.9 或 Clang 或非 GCC 编译器
#if (defined(__GNUC__) &&                                          \
     (__GNUC__ >= 5 || (__GNUC__ >= 4 && __GNUC_MINOR__ >= 9))) || \
    defined(__clang__) || !defined(__GNUC__)
#define SUPPORT_DYNAMIC_TEMPLATE
#endif

template <typename C>
class ResourcePool_l;
template <typename C>
class ResourcePool;

/**
 * @brief 自定义智能指针类，继承自 std::shared_ptr
 * 
 * 这个类扩展了标准的 shared_ptr，添加了与资源池相关的功能
 * 
 * @tparam C 指针所管理的对象类型
 */
template <typename C>
class shared_ptr_imp : public std::shared_ptr<C> {
   public:
    shared_ptr_imp() {}

    /**
     * @brief 构造函数，创建一个带有资源池管理功能的智能指针
     * 
     * @param ptr 原始指针
     * @param weakPool 弱引用的资源池
     * @param quit 控制是否退出资源池循环的标志
     * @param on_recycle 资源回收时的回调函数
     */
    shared_ptr_imp(C *ptr, const std::weak_ptr<ResourcePool_l<C>> &weakPool,
                   std::shared_ptr<std::atomic_bool> quit,
                   const std::function<void(C *)> &on_recycle);

    /**
     * @brief 设置是否退出资源池循环
     * 
     * @param flag 如果为 true，则退出资源池循环；否则继续使用资源池
     * 如果_quit为false, 则在调用shared_ptr的自定义的deleter时，会调用资源池的回收函数
     * 而不是删除对象
     */
    void quit(bool flag = true) {
        if (_quit) {
            *_quit = flag;
        }
    }

   private:
    std::shared_ptr<std::atomic_bool> _quit; // 控制是否退出资源池循环的标志
};

/**
 * @brief 资源池的内部实现类
 * 
 * 这个类管理一个对象池，允许对象的重用以提高性能
 * 
 * @tparam C 池中管理的对象类型
 */
template <typename C>
class ResourcePool_l : public std::enable_shared_from_this<ResourcePool_l<C>> {
   public:
    using ValuePtr = shared_ptr_imp<C>;
    friend class shared_ptr_imp<C>;
    friend class ResourcePool<C>;

    /**
     * @brief 默认构造函数
     * 
     * 初始化分配器函数，用于创建新的对象
     */
    ResourcePool_l() {
        _alloc = []() -> C * { return new C(); };
    }

#if defined(SUPPORT_DYNAMIC_TEMPLATE)
    /**
     * @brief 支持可变参数的构造函数
     * 
     * 允许用自定义参数创建对象
     * 
     * @tparam ArgTypes 可变参数类型
     * @param args 传递给对象构造函数的参数
     */
    template <typename... ArgTypes>
    ResourcePool_l(ArgTypes &&...args) {
        _alloc = [args...]() -> C * { return new C(args...); };
    }
#endif  // defined(SUPPORT_DYNAMIC_TEMPLATE)

    /**
     * @brief 析构函数
     * 
     * 释放池中所有未使用的对象
     */
    ~ResourcePool_l() {
        for (auto ptr : _objs) {
            delete ptr;
        }
    }

    /**
     * @brief 设置资源池的大小
     * 
     * @param size 池的最大容量
     */
    void setSize(size_t size) {
        _pool_size = size;
        _objs.reserve(size);  //  分配内存：
    }

    /**
     * @brief 获取一个对象
     * 
     * @param on_recycle 对象被回收时的回调函数
     * @return ValuePtr 包装了对象的智能指针
     */
    ValuePtr obtain(const std::function<void(C *)> &on_recycle = nullptr) {
        return ValuePtr(getPtr(), _weak_self,
                        std::make_shared<std::atomic_bool>(false), on_recycle);
    }

    /**
     * @brief 获取一个对象（另一种方式）
     * 
     * @return std::shared_ptr<C> 包装了对象的标准智能指针
     */
    std::shared_ptr<C> obtain2() {
        auto weak_self = _weak_self;
        return std::shared_ptr<C>(getPtr(), [weak_self](C *ptr) {
            auto strongPool = weak_self.lock();
            if (strongPool) {
                // 放入循环池
                strongPool->recycle(ptr);
            } else {
                delete ptr;
            }
        });
    }

   private:
    /**
     * @brief 回收一个对象到池中
     * 
     * @param obj 要回收的对象指针
     */
    void recycle(C *obj) {
        auto is_busy = _busy.test_and_set();
        if (!is_busy) {
            // 获取到锁
            if (_objs.size() >= _pool_size) {
                delete obj;
            } else {
                _objs.emplace_back(obj);
            }
            _busy.clear();
        } else {
            // 未获取到锁
            delete obj;
        }
    }

    /**
     * @brief 获取一个可用的对象指针
     * 
     * @return C* 可用对象的指针
     */
    C *getPtr() {
        C *ptr;
        auto is_busy = _busy.test_and_set();  // 将_busy设置为true, 返回_busy的旧值
        if (!is_busy) {
            // 获取到锁
            if (_objs.size() == 0) {
                ptr = _alloc();
            } else {
                ptr = _objs.back();
                _objs.pop_back();
            }
            _busy.clear();
        } else {
            // 未获取到锁
            ptr = _alloc();
        }
        return ptr;
    }

    /**
     * @brief 设置弱引用
     * 
     * 用于在 shared_ptr_imp 中获取资源池的弱引用
     */
    void setup() { _weak_self = this->shared_from_this(); }

   private:
    size_t _pool_size = 8; // 池的最大容量
    std::vector<C *> _objs; // 存储可用对象的向量
    std::function<C *(void)> _alloc; // 分配器函数
    std::atomic_flag _busy{false}; // 标记池是否正在被访问, 原子类型的bool， 必须初始化为false
    std::weak_ptr<ResourcePool_l> _weak_self; // 弱引用自身
};

/**
 * @brief 资源池类，用于管理和复用对象
 * 
 * 这个类提供了一个高效的对象池，允许对象的重用以提高性能
 * 
 * @tparam C 池中管理的对象类型
 */
template <typename C>
class ResourcePool {
   public:
    using ValuePtr = shared_ptr_imp<C>;

    /**
     * @brief 默认构造函数
     * 
     * 初始化内部的 ResourcePool_l 对象
     */
    ResourcePool() {
        pool.reset(new ResourcePool_l<C>());
        pool->setup();
    }

#if defined(SUPPORT_DYNAMIC_TEMPLATE)
    /**
     * @brief 支持可变参数的构造函数
     * 
     * 允许用自定义参数创建对象
     * 
     * @tparam ArgTypes 可变参数类型
     * @param args 传递给对象构造函数的参数
     */
    template <typename... ArgTypes>
    ResourcePool(ArgTypes &&...args) {
        pool = std::make_shared<ResourcePool_l<C>>(
            std::forward<ArgTypes>(args)...);
        pool->setup();
    }
#endif  // defined(SUPPORT_DYNAMIC_TEMPLATE)

    /**
     * @brief 设置资源池的大小
     * 
     * @param size 池的最大容量
     */
    void setSize(size_t size) { pool->setSize(size); }

    /**
     * @brief 获取一个对象（功能丰富版）
     * 
     * @param on_recycle 对象被回收时的回调函数
     * @return ValuePtr 包装了对象的智能指针
     */
    ValuePtr obtain(const std::function<void(C *)> &on_recycle = nullptr) {
        return pool->obtain(on_recycle);
    }

    /**
     * @brief 获取一个对象（性能优化版）
     * 
     * @return std::shared_ptr<C> 包装了对象的标准智能指针
     */
    std::shared_ptr<C> obtain2() { return pool->obtain2(); }

   private:
    std::shared_ptr<ResourcePool_l<C>> pool; // 内部的 ResourcePool_l 对象
};

// shared_ptr_imp 的构造函数实现
template <typename C>
shared_ptr_imp<C>::shared_ptr_imp(
    C *ptr, const std::weak_ptr<ResourcePool_l<C>> &weakPool,
    std::shared_ptr<std::atomic_bool> quit,
    const std::function<void(C *)> &on_recycle)
    : std::shared_ptr<C>(ptr,
                         [weakPool, quit, on_recycle](C *ptr) {
                             if (on_recycle) {
                                 on_recycle(ptr);
                             }
                             auto strongPool = weakPool.lock();
                             if (strongPool && !(*quit)) {
                                 // 循环池还在并且不放弃放入循环池
                                 strongPool->recycle(ptr);
                             } else {
                                 delete ptr;
                             }
                         }),
      _quit(std::move(quit)) {}

} /* namespace toolkit */
#endif /* UTIL_RECYCLEPOOL_H_ */
