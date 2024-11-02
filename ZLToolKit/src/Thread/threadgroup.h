/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef THREADGROUP_H_
#define THREADGROUP_H_

#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace toolkit {

/**
 * @class thread_group
 * @brief 线程组类，用于管理一组线程
 * 
 * 该类提供线程的创建、删除、判断和同步等功能
 */
class thread_group {
   private:
    // 禁用拷贝构造和赋值操作，确保线程组对象不能被复制
    // 这是c++11之前的标准做法，声明为private然后不实现
    thread_group(thread_group const &);
    thread_group &operator=(thread_group const &);

   public:
    /**
     * @brief 默认构造函数
     */
    thread_group() {}

    /**
     * @brief 析构函数，清理所有线程资源
     */
    ~thread_group() { _threads.clear(); }

    /**
     * @brief 判断当前线程是否在线程组中
     * @return true 如果当前线程在线程组中，否则返回 false
     */
    bool is_this_thread_in() {
        auto thread_id = std::this_thread::get_id();
        // 检查是否是最后创建的线程
        if (_thread_id == thread_id) {
            return true;
        }
        // 在线程映射表中查找
        return _threads.find(thread_id) != _threads.end();
    }

    /**
     * @brief 判断指定线程是否在线程组中
     * @param thrd 要检查的线程指针
     * @return true 如果指定线程在线程组中，否则返回 false
     */
    bool is_thread_in(std::thread *thrd) {
        if (!thrd) {
            return false;
        }
        auto it = _threads.find(thrd->get_id());
        return it != _threads.end();
    }

    /**
     * @brief 创建新线程并添加到线程组
     * @tparam F 线程函数类型
     * @param threadfunc 线程函数
     * @return 返回创建的线程指针
     * 
     * 使用智能指针管理线程对象，确保资源安全
     */
    template <typename F>
    std::thread *create_thread(F &&threadfunc) {
        // 创建新线程并用智能指针管理
        auto thread_new =
            std::make_shared<std::thread>(std::forward<F>(threadfunc));
        // 记录最新创建的线程ID
        _thread_id = thread_new->get_id();
        // 将线程添加到映射表
        _threads[_thread_id] = thread_new;
        return thread_new.get();
    }

    /**
     * @brief 从线程组中移除指定线程
     * @param thrd 要移除的线程指针
     */
    void remove_thread(std::thread *thrd) {
        auto it = _threads.find(thrd->get_id());
        if (it != _threads.end()) {
            _threads.erase(it);
        }
    }

    /**
     * @brief 等待所有线程完成
     * @throw std::runtime_error 如果在线程自身中调用此方法
     * 
     * 等待所有线程结束并清理资源。如果在线程自身中调用会抛出异常
     */
    void join_all() {
        if (is_this_thread_in()) {
            throw std::runtime_error("Trying joining itself in thread_group");
        }
        // 等待所有线程完成
        for (auto &it : _threads) {
            if (it.second->joinable()) {
                it.second->join();  //等待线程主动退出
            }
        }
        // 清理线程映射表
        _threads.clear();
    }

    /**
     * @brief 获取线程组中的线程数量
     * @return 返回线程组中的线程数量
     */
    size_t size() { return _threads.size(); }

   private:
    std::thread::id _thread_id;    ///< 最后创建的线程ID, 线程id只是用来标识不是计数的
    std::unordered_map<std::thread::id, std::shared_ptr<std::thread>> _threads;    ///< 线程ID到线程对象的映射表
};

} /* namespace toolkit */
#endif /* THREADGROUP_H_ */
