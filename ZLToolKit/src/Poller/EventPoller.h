/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef EventPoller_h
#define EventPoller_h

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "Network/Buffer.h"
#include "Network/BufferSock.h"
#include "PipeWrap.h"
#include "Thread/TaskExecutor.h"
#include "Thread/ThreadPool.h"
#include "Util/List.h"
#include "Util/logger.h"

#if defined(__linux__) || defined(__linux)
#define HAS_EPOLL
#endif  //__linux__

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
    defined(__OpenBSD__)
#define HAS_KQUEUE
#endif  // __APPLE__

namespace toolkit {

/**
 * @class EventPoller
 * @brief 事件轮询器类，继承自TaskExecutor和AnyStorage
 * 
 * 该类用于管理事件轮询，支持异步任务执行和事件监听
 */
class EventPoller : public TaskExecutor,
                    public AnyStorage,
                    public std::enable_shared_from_this<EventPoller> {
   public:
    friend class TaskExecutorGetterImp;

    using Ptr = std::shared_ptr<EventPoller>;
    using PollEventCB = std::function<void(int event)>;
    using PollCompleteCB = std::function<void(bool success)>;
    using DelayTask = TaskCancelableImp<uint64_t(void)>;

    /**
     * @enum Poll_Event
     * @brief 事件类型枚举
     */
    typedef enum {
        Event_Read = 1 << 0,   ///< 读事件
        Event_Write = 1 << 1,  ///< 写事件
        Event_Error = 1 << 2,  ///< 错误事件
        Event_LT = 1 << 3,     ///< 水平触发
    } Poll_Event;

    /**
     * @brief 析构函数
     */
    ~EventPoller();

    /**
     * @brief 获取EventPollerPool单例中的第一个EventPoller实例
     * 保留该接口是为了兼容老代码
     * @return 单例
     */
    static EventPoller &Instance();

    /**
     * @brief 添加事件监听
     * @param fd 监听的文件描述符
     * @param event 事件类型，例如 Event_Read | Event_Write
     * @param cb 事件回调函数
     * @return -1:失败，0:成功
     */
    int addEvent(int fd, int event, PollEventCB cb);

    /**
     * @brief 删除事件监听
     * @param fd 监听的文件描述符
     * @param cb 删除成功回调函数
     * @return -1:失败，0:成功
     */
    int delEvent(int fd, PollCompleteCB cb = nullptr);

    /**
     * @brief 修改监听事件类型
     * @param fd 监听的文件描述符
     * @param event 事件类型，例如 Event_Read | Event_Write
     * @return -1:失败，0:成功
     */
    int modifyEvent(int fd, int event, PollCompleteCB cb = nullptr);

    /**
     * @brief 异步执行任务
     * @param task 任务
     * @param may_sync 如果调用该函数的线程就是本对象的轮询线程，那么may_sync为true时就是同步执行任务
     * @return 是否成功，一定会返回true
     */
    Task::Ptr async(TaskIn task, bool may_sync = true) override;

    /**
     * @brief 以最高优先级异步执行任务
     * @param task 任务
     * @param may_sync 如果调用该函数的线程就是本对象的轮询线程，那么may_sync为true时就是同步执行任务
     * @return 是否成功，一定会返回true
     */
    Task::Ptr async_first(TaskIn task, bool may_sync = true) override;

    /**
     * @brief 判断执行该接口的线程是否为本对象的轮询线程
     * @return 是否为本对象的轮询线程
     */
    bool isCurrentThread();

    /**
     * @brief 延时执行某个任务
     * @param delay_ms 延时毫秒数
     * @param task 任务，返回值为0时代表不再重复任务，否则为下次执行延时，如果任务中抛异常，那么默认不重复任务
     * @return 可取消的任务标签
     */
    DelayTask::Ptr doDelayTask(uint64_t delay_ms, std::function<uint64_t()> task);

    /**
     * @brief 获取当前线程关联的Poller实例
     * @return 当前线程关联的Poller实例
     */
    static EventPoller::Ptr getCurrentPoller();

    /**
     * @brief 获取当前线程下所有socket共享的读缓存
     * @param is_udp 是否为UDP
     * @return 共享的读缓存
     */
    SocketRecvBuffer::Ptr getSharedBuffer(bool is_udp);

    /**
     * @brief 获取poller线程id
     * @return poller线程id
     */
    std::thread::id getThreadId() const;

    /**
     * @brief 获取线程名
     * @return 线程名
     */
    const std::string &getThreadName() const;

   private:
    /**
     * @brief 构造函数
     * 本对象只允许在EventPollerPool中构造
     * @param name 线程名
     */
    EventPoller(std::string name);

    /**
     * @brief 执行事件轮询
     * @param blocked 是否用执行该接口的线程执行轮询
     * @param ref_self 是否记录本对象到thread local变量
     */
    void runLoop(bool blocked, bool ref_self);

    /**
     * @brief 内部管道事件，用于唤醒轮询线程用
     * @param flush 是否刷新
     */
    void onPipeEvent(bool flush = false);

    /**
     * @brief 切换线程并执行任务
     * @param task 任务
     * @param may_sync 是否允许同步执行
     * @param first 是否优先执行
     * @return 可取消的任务本体，如果已经同步执行，则返回nullptr
     */
    Task::Ptr async_l(TaskIn task, bool may_sync = true, bool first = false);

    /**
     * @brief 结束事件轮询
     * 需要指出的是，一旦结束就不能再次恢复轮询线程
     */
    void shutdown();

    /**
     * @brief 刷新延时任务
     * @param now 当前时间
     * @return 下次执行延时
     */
    uint64_t flushDelayTask(uint64_t now);

    /**
     * @brief 获取select或epoll休眠时间
     * @return 休眠时间
     */
    uint64_t getMinDelay();

    /**
     * @brief 添加管道监听事件
     */
    void addEventPipe();

   private:
    class ExitException : public std::exception {};

    bool _exit_flag;  ///< 标记loop线程是否退出
    std::string _name;  ///< 线程名
    std::weak_ptr<SocketRecvBuffer> _shared_buffer[2];  ///< 当前线程下，所有socket共享的读缓存
    std::thread *_loop_thread = nullptr;  ///< 执行事件循环的线程
    semaphore _sem_run_started;  ///< 通知事件循环的线程已启动
    PipeWrap _pipe;  ///< 内部事件管道
    std::mutex _mtx_task;  ///< 任务互斥锁
    List<Task::Ptr> _list_task;  ///< 从其他线程切换过来的任务
    Logger::Ptr _logger;  ///< 日志指针

#if defined(HAS_EPOLL) || defined(HAS_KQUEUE)
    int _event_fd = -1;  ///< epoll或kqueue文件描述符
    std::unordered_map<int, std::shared_ptr<PollEventCB>> _event_map;  ///< 事件回调映射
#else
    struct Poll_Record {
        using Ptr = std::shared_ptr<Poll_Record>;
        int fd;
        int event;
        int attach;
        PollEventCB call_back;
    };
    std::unordered_map<int, Poll_Record::Ptr> _event_map;  ///< 事件回调映射
#endif  // HAS_EPOLL
    std::unordered_set<int> _event_cache_expired;  ///< 过期事件缓存

    std::multimap<uint64_t, DelayTask::Ptr> _delay_task_map;  ///< 定时任务映射
};

/**
 * @class EventPollerPool
 * @brief 事件轮询器池类，管理多个EventPoller实例
 */
class EventPollerPool : public std::enable_shared_from_this<EventPollerPool>,
                        public TaskExecutorGetterImp {
   public:
    using Ptr = std::shared_ptr<EventPollerPool>;
    static const std::string kOnStarted;
#define EventPollerPoolOnStartedArgs EventPollerPool &pool, size_t &size

    /**
     * @brief 析构函数
     */
    ~EventPollerPool() = default;

    /**
     * @brief 获取EventPollerPool单例实例
     * @return EventPollerPool& 单例实例的引用
     */
    static EventPollerPool &Instance();

    /**
     * @brief 设置EventPoller实例的数量
     * 此方法必须在EventPollerPool单例创建之前调用才有效
     * 如果不调用此方法，默认创建thread::hardware_concurrency()个EventPoller实例
     * @param size EventPoller实例的数量，如果为0则使用thread::hardware_concurrency()
     */
    static void setPoolSize(size_t size = 0);

    /**
     * @brief 设置是否启用CPU亲和性
     * 控制内部创建的线程是否设置CPU亲和性，默认启用
     * @param enable 是否启用CPU亲和性
     */
    static void enableCpuAffinity(bool enable);

    /**
     * @brief 获取第一个EventPoller实例
     * @return EventPoller::Ptr 第一个EventPoller实例的智能指针
     */
    EventPoller::Ptr getFirstPoller();

    /**
     * @brief 获取负载较轻的EventPoller实例
     * 根据负载情况选择一个轻负载的实例
     * 如果当前线程已经绑定了一个EventPoller，则优先返回当前线程的实例
     * 返回当前线程的实例可以提高线程安全性
     * @param prefer_current_thread 是否优先获取当前线程
     * @return EventPoller::Ptr 选中的EventPoller实例的智能指针
     */
    EventPoller::Ptr getPoller(bool prefer_current_thread = true);

    /**
     * @brief 设置 getPoller() 是否优先返回当前线程
     * 在批量创建Socket对象时，如果优先返回当前线程，
     * 那么将导致负载不够均衡，所以可以暂时关闭然后再开启
     * @param flag 是否优先返回当前线程
     */
    void preferCurrentThread(bool flag = true);

   private:
    /**
     * @brief 构造函数
     * 构造函数被声明为private，以支持单例模式
     */
    EventPollerPool();

   private:
    bool _prefer_current_thread = true;  ///< 是否优先返回当前线程
};

}  // namespace toolkit
#endif /* EventPoller_h */
