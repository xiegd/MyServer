#ifndef _EVENTPOLLER_H_
#define _EVENTPOLLER_H_

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <thread>

#include "taskexecutor.h"
#include "buffersock.h"
#include "utility.h"
#include "logger.h"

namespace xkernel {

class PipeWrap;

/*
    brief: 事件轮询线程, 对应一个EventPoller实例
*/
class EventPoller : public TaskExecutor, 
                    public AnyStorage,
                    public std::enable_shared_from_this<EventPoller> {
public:
    friend class TaskExecutorGetterImpl;

    enum class Poll_Event {
        None_Event = 0,
        Read_Event = 1 << 0,
        Write_Event = 1 << 1,
        Error_Event = 1 << 2,
        Event_LT = 1 << 3,
    };

    using Ptr = std::shared_ptr<EventPoller>;
    using PollEventCb = std::function<void(Poll_Event event)>;
    using PollCompleteCb = std::function<void(bool success)>;
    using DelayTask = TaskCancelableImpl<uint64_t(void)>;

    static EventPoller& Instance(); 
    ~EventPoller();

public:
    int addEvent(int fd, Poll_Event event, PollEventCb cb);
    int delEvent(int fd, PollCompleteCb cb = nullptr);
    int modifyEvent(int fd, Poll_Event event, PollCompleteCb cb = nullptr);

    Task::Ptr async(TaskIn task, bool may_sync = true) override;
    Task::Ptr asyncFirst(TaskIn task, bool may_sync = true) override;

    bool isCurrentThread();  // 判断执行该接口的线程是否为本对象的轮询线程
    DelayTask::Ptr doDelayTask(uint64_t delay_ms, std::function<uint64_t()> task);
    static EventPoller::Ptr getCurrentPoller();  // 获取当前线程关联的Poller实例
    SocketRecvBuffer::Ptr getSharedBuffer(bool is_udp);  // 获取当前线程下所有socket共享的读缓存
    std::thread::id getThreadId() const;
    const std::string& getThreadName() const;

private:
    EventPoller(std::string name);

    void runLoop(bool blocked, bool ref_self);  // 事件循环核心
    void shutdown();
    void onPipeEvent(bool flush = false);  // 内部管道事件，用于唤醒轮询线程
    Task::Ptr async_l(TaskIn task, bool may_sync = true, bool first = false);
    uint64_t flushDelayTask(uint64_t now_time);
    uint64_t getMinDelay();
    void addEventPipe();

private:
    class ExitException : public std::exception {};

    bool exit_flag_;  // 标记loop线程是否退出
    std::string name_;  // 线程名
    std::weak_ptr<SocketRecvBuffer> shared_buffer_[2];  // 当前线程下，所有socket共享的读缓存
    std::thread* loop_thread_ = nullptr;
    semaphore sem_run_started_;
    std::unique_ptr<PipeWrap> pipe_;
    std::mutex mtx_task_;
    List<Task::Ptr> list_task_;  // 任务队列, 使用async() or asyncFirst() 添加, 通过pipe_的读端唤醒
    Logger::Ptr logger_;
    int event_fd_ = -1;  // epoll实例的fd
    std::unordered_map<int, std::shared_ptr<PollEventCb>> event_map_;  // fd到回调函数的映射, fd, cb
    std::unordered_set<int> event_cache_expired_;  // 已过期事件的缓存
    std::multimap<uint64_t, DelayTask::Ptr> delay_task_map_;  // 定时任务映射 
};


constexpr EventPoller::Poll_Event operator|(EventPoller::Poll_Event lhs, EventPoller::Poll_Event rhs) {
    return static_cast<EventPoller::Poll_Event>(
        static_cast<std::underlying_type_t<EventPoller::Poll_Event>>(lhs)
        | static_cast<std::underlying_type_t<EventPoller::Poll_Event>>(rhs)
    );
}

constexpr EventPoller::Poll_Event operator&(EventPoller::Poll_Event lhs, EventPoller::Poll_Event rhs) {
    return static_cast<EventPoller::Poll_Event>(
        static_cast<std::underlying_type_t<EventPoller::Poll_Event>>(lhs)
        & static_cast<std::underlying_type_t<EventPoller::Poll_Event>>(rhs)
    );
}

// 重载!运算符，对event & Poll_event::xx后的结果判断事件是否包含对应的事件
constexpr bool operator!(EventPoller::Poll_Event event) {
    return static_cast<std::underlying_type_t<EventPoller::Poll_Event>>(event) != 0;
}

/*
    brief: 事件轮询线程池, 每个线程对应一个EventPoller实例
*/
class EventPollerPool : public std::enable_shared_from_this<EventPollerPool>,
                        public TaskExecutorGetterImpl {
public:
    using Ptr = std::shared_ptr<EventPollerPool>;
    static const std::string KOnStarted;
#define EventPollerPoolOnStartedArgs EventPollerPool& pool, size_t& size

    ~EventPollerPool() = default;
    static EventPollerPool& Instance();
    static void setPoolSize(size_t size = 0);  // 必须在创建EventPollerPool实例之前调用才有效
    static void enableCpuAffinity(bool enable);

    EventPoller::Ptr getFirstPoller();
    EventPoller::Ptr getPoller(bool prefer_current_thread = true);  // 根据负载情况选择Poller
    void preferCurrentThread(bool flag = true);  // 设置getPoller()是否优先返回当前线程

private:
    EventPollerPool();

private:
    bool prefer_current_thread_ = true;
};
}  // namespace xkernel
#endif  // _EVENTPOLLER_H_