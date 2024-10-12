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
 * @file NoticeCenter.h
 * @brief 实现了一个事件通知中心
 * 
 * 主要功能：
 * 1. 提供了一个中央事件分发机制，允许在不同组件间进行松耦合的通信。
 * 2. 支持多种类型的事件监听和触发。
 * 3. 允许动态添加和删除事件监听器。
 * 4. 提供了线程安全的事件触发机制。
 * 5. 实现了一个轻量级的观察者模式。
 */

#ifndef SRC_UTIL_NOTICECENTER_H_
#define SRC_UTIL_NOTICECENTER_H_

#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "function_traits.h"
#include "util.h"

namespace toolkit {

/**
 * @class EventDispatcher
 * @brief 事件分发器类，负责管理和触发特定事件的监听器
 */
class EventDispatcher {
   public:
    friend class NoticeCenter;
    using Ptr = std::shared_ptr<EventDispatcher>;

    ~EventDispatcher() = default;

   private:
    using MapType = std::unordered_multimap<void *, Any>;

    EventDispatcher() = default;

    /**
     * @class InterruptException
     * @brief 用于中断事件传播的异常类
     */
    class InterruptException : public std::runtime_error {
       public:
        InterruptException() : std::runtime_error("InterruptException") {}
        ~InterruptException() {}
    };

    /**
     * @brief 触发事件
     * @param safe 是否安全触发（捕获异常）
     * @param args 事件参数
     * @return 成功触发的监听器数量
     */
    template <typename... ArgsType>
    int emitEvent(bool safe, ArgsType &&...args) {
        // 因为std::forward不能数组/函数类型不退化为指针，所以使用了decltype, 保持类型
        using stl_func = std::function<void(decltype(std::forward<ArgsType>(args))...)>;
        decltype(_mapListener) copy;
        {
            // 先拷贝(开销比较小)，目的是防止在触发回调时还是上锁状态从而导致交叉互锁
            std::lock_guard<std::recursive_mutex> lck(_mtxListener);
            copy = _mapListener;
        }

        int ret = 0;
        // 遍历所有事件监听器,如果类型匹配则调用函数，如果抛出了InterruptException则退出
        // 其他类型异常，包括类型不匹配，则忽略
        for (auto &pr : copy) {
            try {
                // 使用Any的get方法获取到stl_func类型的函数，然后调用
                pr.second.get<stl_func>(safe)(std::forward<ArgsType>(args)...);
                ++ret;
            } catch (InterruptException &) {
                // 虽然这里只明确捕获了InterruptException，但是其他异常也会被捕获
                
                ++ret;
                break;
            }
        }
        return ret;
    }

    /**
     * @brief 添加事件监听器
     * @param tag 监听器标签
     * @param func 监听器函数
     */
    template <typename FUNC>
    void addListener(void *tag, FUNC &&func) {
        // type，移除引用后的类型
        using stl_func = typename function_traits<
            typename std::remove_reference<FUNC>::type>::stl_function_type;
        Any listener;
        // 使用function_traits获取func的类型, 然后使用std::forward完美转发func
        // 这里只显式指定了部分模板参数，别的让编译器从函数参数中推导(函数参数中有，可以推导)
        listener.set<stl_func>(std::forward<FUNC>(func));
        std::lock_guard<std::recursive_mutex> lck(_mtxListener);
        // 因为ummap允许重复的键，所以使用emplace总是会插入新的元素
        _mapListener.emplace(tag, listener);
    }

    /**
     * @brief 删除事件监听器
     * @param tag 监听器标签
     * @param empty 输出参数，指示监听器列表是否为空
     */
    void delListener(void *tag, bool &empty) {
        std::lock_guard<std::recursive_mutex> lck(_mtxListener);
        _mapListener.erase(tag);
        empty = _mapListener.empty();
    }

   private:
    std::recursive_mutex _mtxListener;
    MapType _mapListener;
};

/**
 * @class NoticeCenter
 * @brief 通知中心类，管理所有事件和监听器
 */
class NoticeCenter : public std::enable_shared_from_this<NoticeCenter> {
   public:
    using Ptr = std::shared_ptr<NoticeCenter>;

    /**
     * @brief 获取NoticeCenter单例
     * @return NoticeCenter单例引用
     */
    static NoticeCenter &Instance();

    /**
     * @brief 触发事件
     * @param event 事件名称
     * @param args 事件参数
     * @return 成功触发的监听器数量
     */
    template <typename... ArgsType>
    int emitEvent(const std::string &event, ArgsType &&...args) {
        return emitEvent_l(false, event, std::forward<ArgsType>(args)...);
    }

    /**
     * @brief 安全触发事件（捕获异常）
     * @param event 事件名称
     * @param args 事件参数
     * @return 成功触发的监听器数量
     */
    template <typename... ArgsType>
    int emitEventSafe(const std::string &event, ArgsType &&...args) {
        return emitEvent_l(true, event, std::forward<ArgsType>(args)...);
    }

    /**
     * @brief 添加事件监听器
     * @param tag 监听器标签
     * @param event 事件名称
     * @param func 监听器函数
     */
    template <typename FUNC>
    void addListener(void *tag, const std::string &event, FUNC &&func) {
        getDispatcher(event, true)->addListener(tag, std::forward<FUNC>(func));
    }

    /**
     * @brief 删除特定事件的监听器
     * @param tag 监听器标签
     * @param event 事件名称
     */
    void delListener(void *tag, const std::string &event) {
        auto dispatcher = getDispatcher(event);
        if (!dispatcher) {
            // 不存在该事件
            return;
        }
        bool empty;
        dispatcher->delListener(tag, empty);
        if (empty) {
            delDispatcher(event, dispatcher);
        }
    }

    /**
     * @brief 删除所有事件中的特定标签监听器(性能较差)
     * @param tag 监听器标签
     */
    void delListener(void *tag) {
        std::lock_guard<std::recursive_mutex> lck(_mtxListener);
        bool empty;
        for (auto it = _mapListener.begin(); it != _mapListener.end();) {
            it->second->delListener(tag, empty);
            if (empty) {
                it = _mapListener.erase(it);
                continue;
            }
            ++it;
        }
    }

    /**
     * @brief 清除所有事件和监听器
     */
    void clearAll() {
        std::lock_guard<std::recursive_mutex> lck(_mtxListener);
        _mapListener.clear();
    }

   private:
    template <typename... ArgsType>
    int emitEvent_l(bool safe, const std::string &event, ArgsType &&...args) {
        auto dispatcher = getDispatcher(event);
        if (!dispatcher) {
            // 该事件无人监听
            return 0;
        }
        return dispatcher->emitEvent(safe, std::forward<ArgsType>(args)...);
    }

    EventDispatcher::Ptr getDispatcher(const std::string &event,
                                       bool create = false) {
        std::lock_guard<std::recursive_mutex> lck(_mtxListener);
        auto it = _mapListener.find(event);
        if (it != _mapListener.end()) {
            return it->second;
        }
        if (create) {
            // 如果为空则创建一个
            EventDispatcher::Ptr dispatcher(new EventDispatcher());
            _mapListener.emplace(event, dispatcher);
            return dispatcher;
        }
        return nullptr;
    }

    void delDispatcher(const std::string &event,
                       const EventDispatcher::Ptr &dispatcher) {
        std::lock_guard<std::recursive_mutex> lck(_mtxListener);
        auto it = _mapListener.find(event);
        if (it != _mapListener.end() && dispatcher == it->second) {
            // 两者相同则删除
            _mapListener.erase(it);
        }
    }

   private:
    std::recursive_mutex _mtxListener;
    std::unordered_map<std::string, EventDispatcher::Ptr> _mapListener;
};

/**
 * @struct NoticeHelper
 * @brief 辅助结构体，用于简化事件触发
 */
template <typename T>
struct NoticeHelper;

template <typename RET, typename... Args>
struct NoticeHelper<RET(Args...)> {
   public:
    /**
     * @brief 触发事件的静态方法
     * @param event 事件名称
     * @param args 事件参数
     * @return 成功触发的监听器数量
     */
    template <typename... ArgsType>
    static int emit(const std::string &event, ArgsType &&...args) {
        return emit(NoticeCenter::Instance(), event,
                    std::forward<ArgsType>(args)...);
    }

    /**
     * @brief 在指定NoticeCenter实例上触发事件的静态方法
     * @param center NoticeCenter实例
     * @param event 事件名称
     * @param args 事件参数
     * @return 成功触发的监听器数量
     */
    template <typename... ArgsType>
    static int emit(NoticeCenter &center, const std::string &event,
                    ArgsType &&...args) {
        return center.emitEventSafe(event, std::forward<Args>(args)...);
    }
};

/**
 * @brief 用于触发事件的宏
 */
#define NOTICE_EMIT(types, ...) NoticeHelper<void(types)>::emit(__VA_ARGS__)

} /* namespace toolkit */
#endif /* SRC_UTIL_NOTICECENTER_H_ */