/**
 * @file noticecenter.h
 * @brief 实现了一个事件通知中心
 * 
 * 主要功能：
 * 1. 提供了一个中央事件分发机制，允许在不同组件间进行松耦合的通信。
 * 2. 支持多种类型的事件监听和触发。
 * 3. 允许动态添加和删除事件监听器。
 * 4. 提供了线程安全的事件触发机制。
 * 5. 实现了一个轻量级的观察者模式。
 * enventCenter管理所有事件的监听器, 即_EventListeners
 * 每个事件有很多个不同标签的监听器, 即_tagListener 
 * _EventListeners是一个map, key是事件名称, value是_tagListener
 * _tagListener是一个multimap, key是tag, value是Any类型, 内部存储了对应的函数
 * 触发事件： 某个事件发生后，传递要触发的函数的参数， 遍历对应事件的_tagListener, 调用对应的函数
 */

#ifndef _NOTICECENTER_H_
#define _NOTICECENTER_H_

#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "function_traits.h"
#include "utility.h"

namespace xkernel {

// 事件分发器类，负责管理和触发特定事件的监听器
class EventDispatcher {
public:
    friend class NoticeCenter;
    using Ptr = std::shared_ptr<EventDispatcher>;

    ~EventDispatcher() = default;

private:
    EventDispatcher() = default;

    // 用于中断事件传播的异常类
    class InterruptException : public std::runtime_error {
    public:
        InterruptException() : std::runtime_error("InterruptException") {}
        ~InterruptException() {}
    };

    // 触发事件监听器中的所有函数， 返回成功触发的数量
    template <typename... ArgsType>
    int emitEvent(bool safe, ArgsType &&...args) {
        using stl_func = std::function<void(decltype(std::forward<ArgsType>(args))...)>;
        decltype(_tagListeners) copy;
        {
            // 先拷贝(开销比较小)，目的是防止在触发回调时还是上锁状态从而导致交叉互锁
            std::lock_guard<std::recursive_mutex> lck(_mtxListener);
            copy = _tagListeners;
        }

        int ret = 0;
        // 遍历所有事件监听器,如果类型匹配则调用函数，如果抛出了InterruptException则退出
        // 其他类型异常，包括类型不匹配，则忽略
        for (auto &pr : copy) {
            try {
                pr.second.get<stl_func>(safe)(std::forward<ArgsType>(args)...);  // 获取stl_func类型的函数，然后调用
                ++ret;  // 怎么处理参数不匹配/为空的std::invalid_argument异常？
            } catch (InterruptException &) {
                ++ret;
                break;
            }
        }
        return ret;
    }

    // 添加标签为tag的事件监听器
    template <typename FUNC>
    void addListener(void *tag, FUNC &&func) {
        // 获得FUNC对应的std::function类型
        using stl_func = typename function_traits<
            typename std::remove_reference<FUNC>::type>::stl_function_type;
        Any listener;
        listener.set<stl_func>(std::forward<FUNC>(func));
        std::lock_guard<std::recursive_mutex> lck(_mtxListener);
        _tagListeners.emplace(tag, listener);
    }

    // 删除所有key为tag的事件监听器
    void delListener(void *tag, bool &empty) {
        std::lock_guard<std::recursive_mutex> lck(_mtxListener);
        _tagListeners.erase(tag);
        empty = _tagListeners.empty();
    }

private:
    using MapType = std::unordered_multimap<void*, Any>;

    std::recursive_mutex _mtxListener;
    MapType _tagListeners;
};

// 通知中心类，管理所有事件和监听器
class NoticeCenter : public std::enable_shared_from_this<NoticeCenter> {
public:
    using Ptr = std::shared_ptr<NoticeCenter>;

    static NoticeCenter &Instance();  // 获取NoticeCenter单例

    // 触发事件，返回成功触发的监听器数量
    template <typename... ArgsType>
    int emitEvent(const std::string &event, ArgsType &&...args) {
        return emitEvent_l(false, event, std::forward<ArgsType>(args)...);
    }

    // 安全触发事件（捕获异常），触发相应事件中所有匹配的监听器，返回成功触发的数量
    template <typename... ArgsType>
    int emitEventSafe(const std::string &event, ArgsType &&...args) {
        return emitEvent_l(true, event, std::forward<ArgsType>(args)...);
    }

    // 添加某个事件特定标签的监听器
    template <typename FUNC>
    void addListener(void *tag, const std::string &event, FUNC &&func) {
        getDispatcher(event, true)->addListener(tag, std::forward<FUNC>(func));
    }

    // 删除特定事件的特定标签的监听器
    void delListener(void *tag, const std::string &event) {
        auto dispatcher = getDispatcher(event);
        if (!dispatcher) {
            return;  // 不存在该事件
        }
        bool empty;
        dispatcher->delListener(tag, empty);
        if (empty) {
            delDispatcher(event, dispatcher);
        }
    }

    // 删除所有事件中的特定标签监听器(性能较差)
    void delListener(void *tag) {
        std::lock_guard<std::recursive_mutex> lck(_mtxListener);
        bool empty;
        for (auto it = _eventListeners.begin(); it != _eventListeners.end();) {
            it->second->delListener(tag, empty);
            if (empty) {
                it = _eventListeners.erase(it);
                continue;
            }
            ++it;
        }
    }

    // 清除所有事件和监听器
    void clearAll() {
        std::lock_guard<std::recursive_mutex> lck(_mtxListener);
        _eventListeners.clear();
    }

private:
    template <typename... ArgsType>
    int emitEvent_l(bool safe, const std::string &event, ArgsType &&...args) {
        auto dispatcher = getDispatcher(event);
        if (!dispatcher) {
            return 0;  // 该事件无人监听
        }
        return dispatcher->emitEvent(safe, std::forward<ArgsType>(args)...);
    }

    EventDispatcher::Ptr getDispatcher(const std::string &event, bool create = false) {
        std::lock_guard<std::recursive_mutex> lck(_mtxListener);
        auto it = _eventListeners.find(event);
        if (it != _eventListeners.end()) {
            return it->second;
        }
        if (create) {
            // 如果为空则创建一个
            EventDispatcher::Ptr dispatcher(new EventDispatcher());
            _eventListeners.emplace(event, dispatcher);
            return dispatcher;
        }
        return nullptr;
    }

    void delDispatcher(const std::string &event, const EventDispatcher::Ptr &dispatcher) {
        std::lock_guard<std::recursive_mutex> lck(_mtxListener);
        auto it = _eventListeners.find(event);
        if (it != _eventListeners.end() && dispatcher == it->second) {
            _eventListeners.erase(it);  // 两者相同则删除
        }
    }

private:
    std::recursive_mutex _mtxListener;
    std::unordered_map<std::string, EventDispatcher::Ptr> _eventListeners;
};

// 辅助结构体，用于简化事件触发
template <typename T>
struct NoticeHelper;

template <typename RET, typename... Args>
struct NoticeHelper<RET(Args...)> {
public:
    // 触发事件的静态方法
    template <typename... ArgsType>
    static int emit(const std::string &event, ArgsType &&...args) {
        return emit(NoticeCenter::Instance(), event,
                    std::forward<ArgsType>(args)...);
    }

    // 在指定NoticeCenter实例上触发事件的静态方法
    // NoticeCneter不是一个单例类？
    template <typename... ArgsType>
    static int emit(NoticeCenter &center, const std::string &event,
                    ArgsType &&...args) {
        return center.emitEventSafe(event, std::forward<Args>(args)...);
    }
};

//  用于触发通知的宏
#define NOTICE_EMIT(types, ...) NoticeHelper<void(types)>::emit(__VA_ARGS__)

} /* namespace xkernel */
#endif /* _NOTICECENTER_H_ */
