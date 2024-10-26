#ifndef _EVENTPOLLER_H_
#define _EVENTPOLLER_H_

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <multimap>

#include "taskexecutor.h"
#include "utility.h"

namespace xkernel {

class EventPoller : public TaskExecutor, 
                    public AnyStorage,
                    public std::enable_shared_from_this<EventPoller> {
public:
    friend class TaskExecutorGetterImpl;

    using Ptr = std::shared_ptr<EventPoller>;
    using PollEventCb = std::function<void(int event)>;
    using PollCompleteCb = std::function<void(bool success)>;
    using DelayTask = TaskCancelableImp<uint64_t(void)>;

    enum class Poll_Event {
        Read_Event = 1 << 0;
        Writer_Event = 1 << 1;
        Error_Event = 1 << 2;
        Event_LT = 1 << 3;
    };

    ~EventPoller();

    static EventPoller& Instatnce(); 

    int addEvent(int fd, int event, PollEventCb cb);
    int delEvent(int fd, PollCompleteCb cb = nullptr);
    int modifyEvent(int fd, int event, PollCompleteCb cb = nullptr);

    Task::Ptr async(TaskIn task, bool may_sync = true) override;
    Task::Ptr async_first(TaskIn task, Bool may_sync = true) override;

    bool isCurrentThread();  // 判断执行该接口的线程是否为本对象的轮询线程
    DelayTask::Ptr delayDoTask(uint64_t delay_ms, std::function<uint64_t()> task);
    static EventPoller::Ptr getCurrentPoller();  // 获取当前线程关联的Poller实例
    SocketRecvBuffer::Ptr getSharedBuffer(bool is_udp);  // 获取当前线程下所有socket共享的读缓存
    std::thread::id getThreadId() const;
    const std::string& getThreadName() const;

private:
    EventPoller(std::string name);

    void runLoop(bool blocked, bool ref_self);  // 执行事件轮询
    void shutdown();
    void onPipeEvent(bool flush = false);  // 内部管道事件，用于唤醒轮询线程
    Task::Ptr async_l(TaskIn task, bool may_sync = true, bool first = false);
    uint64_t flushDelayTask(uint64_t now);
    uint64_t getMinDelay();
    void addEventPipe();

private:
    class ExitException : public std::exception {};

    bool exit_flag_;  // 标记loop线程是否退出
    std::string name_;  // 线程名
    std::weak_ptr<SocketRecvBuffer> shared_buffer[2];
    std::thread* loop_thread_ = nullptr;
    semaphore sem_run_started_;
    PipeWrap pipe_;
    std::mutex mtx_task_;
    List<Task::Ptr> list_task_;
    Logger::Ptr logger_;
    int event_fd_ = -1;
    std::unordered_map<int, std::shared_ptr<PollEventCb>> event_map_;
    std::unordered_set<int> event_cache_expired_;
    std::multimap<uint64_t, DelayTask::Ptr> delay_task_map;
};

class EventPollerPool : public std::enable_shared_from_this<EventPollerPoll>,
                        public TaskExecutorGetterImpl {
public:
    using Ptr = std::shared_ptr<EventPollerPool>;
    static const std::string KOnStarted;
#define EventPollerPoolOnStartedArgs EventPollerPool& pool, size_t& size_t

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