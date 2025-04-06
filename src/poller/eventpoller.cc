#include "eventpoller.h"

#include <sys/epoll.h>
#include <unistd.h>
#include <thread>

#include "sockutil.h"
#include "timeticker.h"
#include "uv_errno.h"
#include "utility.h"
#include "threadpool.h"
// #include "taskexecutor.h"
#include "pipe.h"
#include "noticecenter.h"

namespace xkernel {

// epoll事件标志是uint32_t类型
inline uint32_t toEpoll(EventPoller::Poll_Event event) noexcept {
    auto event_i = static_cast<uint32_t>(event);
    return ((event_i & 1 << 0) ? EPOLLIN : 0) |
           ((event_i & 1 << 1) ? EPOLLOUT : 0) |
           ((event_i & 1 << 2) ? (EPOLLHUP | EPOLLERR) : 0) |
           ((event_i & 1 << 3) ? 0 : EPOLLET);
}

inline EventPoller::Poll_Event toPoller(uint32_t epoll_event) noexcept {
    uint32_t event_i = ((epoll_event & (EPOLLIN | EPOLLRDNORM | EPOLLHUP)) ? 1 << 0 : 0) |
           ((epoll_event & (EPOLLOUT | EPOLLWRNORM)) ? 1 << 1 : 0) |
           ((epoll_event & EPOLLHUP) ? 1 << 2 : 0) |
           ((epoll_event & EPOLLERR) ? 1 << 2 : 0);
    return static_cast<EventPoller::Poll_Event>(event_i);
}

#define EPOLL_SIZE 1024
#define create_event() epoll_create(EPOLL_SIZE)

//////////////////////////////// EventPoller /////////////////////////////////

EventPoller& EventPoller::Instance() {
    return *(EventPollerPool::Instance().getFirstPoller());
}

EventPoller::~EventPoller() {
    shutdown();
    if (event_fd_ != -1) {
        close(event_fd_);
        event_fd_ = -1;
    }
    InfoL << "Enter onPipeEvent";
    onPipeEvent(true);  // 退出前清理管道中的数据
    InfoL << getThreadName();
}

/*
    brief: 添加事件
    fd: 监听的fd
    event: 监听的事件类型
    cb: 对应事件发生时的回调函数
*/
int EventPoller::addEvent(int fd, Poll_Event event, PollEventCb cb) {
    TimeTicker();  // 计时宏，用来统计代码执行时间
    if (!cb) {
        WarnL << "PollEventCB is empty";
        return -1;
    }
    // 如果当前线程就是事件循环线程，则直接操作epoll实例
    if (isCurrentThread()) {
        struct epoll_event ev = {0};
        ev.events = toEpoll(event);
        ev.data.fd = fd;
        int ret = epoll_ctl(event_fd_, EPOLL_CTL_ADD, fd, &ev);
        if (ret != -1) {
            event_map_.emplace(fd, std::make_shared<PollEventCb>(std::move(cb)));
        }
        return ret;
    }
    // 如果当前线程不是事件轮询器线程，则异步处理
    async([this, fd, event, cb]() mutable {
        addEvent(fd, event, std::move(cb));
    });
    return 0;
}

int EventPoller::delEvent(int fd, PollCompleteCb cb) {
    TimeTicker();
    if (!cb) {
        cb = [](bool success) {};
    }
    if (isCurrentThread()) {
        int ret = -1;
        if (event_map_.erase(fd)) {
            event_cache_expired_.emplace(fd);
            ret = epoll_ctl(event_fd_, EPOLL_CTL_DEL, fd, nullptr);
        }
        DebugL << "delEvent";
        cb(ret != -1);
        return ret;
    }
    async([this, fd, cb]() mutable {
        delEvent(fd, std::move(cb));
    });
    return 0;
}

int EventPoller::modifyEvent(int fd, Poll_Event event, PollCompleteCb cb) {
    TimeTicker();
    if (!cb) {
        cb = [](bool success) {};
    }

    if (isCurrentThread()) {
        struct epoll_event ev = {0};
        ev.events = toEpoll(event);
        ev.data.fd = fd;
        auto ret = epoll_ctl(event_fd_, EPOLL_CTL_MOD, fd, &ev);
        cb(ret != -1);
        return ret;
    }
    async([this, fd, event, cb]() mutable {
        modifyEvent(fd, event, std::move(cb));
    });
    return 0;
}

Task::Ptr EventPoller::async(TaskIn task, bool may_sync) {
    return async_l(std::move(task), may_sync, false);
}

Task::Ptr EventPoller::asyncFirst(TaskIn task, bool may_sync) {
    return async_l(std::move(task), may_sync, true);
}

Task::Ptr EventPoller::async_l(TaskIn task, bool may_sync, bool first) {
    TimeTicker();
    if (may_sync && isCurrentThread()) {
        task();
        return nullptr;
    }

    auto ret = std::make_shared<Task>(std::move(task));
    {
        std::lock_guard<decltype(mtx_task_)> lck(mtx_task_);
        if (first) {
            list_task_.emplace_front(ret);
        } else {
            list_task_.emplace_back(ret);
        }
    }
    pipe_->write("", 1);
    return ret;
}

// 判断当前线程是否是eventpoller线程
bool EventPoller::isCurrentThread() {
    return !loop_thread_ || loop_thread_->get_id() == std::this_thread::get_id();
}

EventPoller::DelayTask::Ptr EventPoller::doDelayTask(uint64_t delay_ms, std::function<uint64_t()> task) {
    DelayTask::Ptr ret = std::make_shared<DelayTask>(std::move(task));
    auto time_line = TimeUtil::getCurrentMillisecond() + delay_ms;
    asyncFirst([time_line, ret, this]() {
        delay_task_map_.emplace(time_line, ret);
    });
    return ret;
}

static thread_local std::weak_ptr<EventPoller> s_current_poller;

EventPoller::Ptr EventPoller::getCurrentPoller() { return s_current_poller.lock(); }

/*
    brief: 获取共享的读缓冲区, 如果当前线程没有共享的读缓冲区，则创建
    这里使用了工厂方法模式，根据tcp or udp创建不同的SocketRecvBuffer对象
    对应使用recvfrom() or recvmmsg()
*/
SocketRecvBuffer::Ptr EventPoller::getSharedBuffer(bool is_udp) {
    auto ret = shared_buffer_[is_udp].lock();
    if (!ret) {
        ret = SocketRecvBuffer::create(is_udp);
        shared_buffer_[is_udp] = ret;
    }
    return ret;
}

std::thread::id EventPoller::getThreadId() const {
    return loop_thread_ ? loop_thread_->get_id() : std::thread::id();
}

const std::string& EventPoller::getThreadName() const { return name_; }

/*
    brief: 构造函数, addPoller()方法中调用创建EventPollerPool中的EventPoller实例
*/
EventPoller::EventPoller(std::string name) {
    event_fd_ = create_event();
    if (event_fd_ == -1) {
        throw std::runtime_error(StrPrinter << "Create event fd failed: " << get_uv_errmsg());
    }
    SockUtil::setCloExec(event_fd_);
    name_ = std::move(name);
    logger_ = Logger::Instance().shared_from_this();
    pipe_ = std::make_unique<PipeWrap>();
    addEventPipe();
}

/*
    brief: 事件循环核心
    param:
        blocked: 是否阻塞
        ref_self: 是否引用当前实例
*/
void EventPoller::runLoop(bool blocked, bool ref_self) {
    if (blocked) {
        if (ref_self) {
            s_current_poller = shared_from_this();
        }
        sem_run_started_.post();  // 通知主线程，事件循环线程已启动
        exit_flag_ = false;
        uint64_t minDelay;

        uint64_t last_time = TimeUtil::getCurrentMillisecond();
        
        struct epoll_event events[EPOLL_SIZE];
        // 事件循环
        while (!exit_flag_) {
            minDelay = getMinDelay();  // 执行delay task，返回最近延时
            startSleep();  // 统计上一段执行任务的时间 
            // 使用epoll_wait等待事件发生，超时时间由minDelay决定, 
            // 如果minDelay为0，则不设置超时时间，一直等待事件发生
            // epoll_wait返回值为发生的事件数量, 并将发生的事件写入events数组中
            int ret = epoll_wait(event_fd_, events, EPOLL_SIZE, minDelay ? minDelay : -1);
            sleepWakeUp(); // 统计sleep时间 
            if (ret <= 0) {
                continue;  // 超时或被打断
            }

            // if (TimeUtil::getCurrentMillisecond() - last_time > 2000) {
            //     InfoL << "--------------------------------";
            //     DebugL << "list_task_ size: " << list_task_.size();
            //     DebugL << "event_map_ size: " << event_map_.size();
            //     DebugL << "event_cache_expired_ size: " << event_cache_expired_.size(); 
            //     DebugL << "BufferRaw::counter_ size: " << ObjectCounter<BufferRaw>::count();  // 
            //     InfoL << "--------------------------------";
            //     last_time = TimeUtil::getCurrentMillisecond();
            // }

            event_cache_expired_.clear();
            // 处理就绪事件
            for (int i = 0; i < ret; ++i) {
                struct epoll_event& ev = events[i];
                int fd = ev.data.fd;
                if (event_cache_expired_.count(fd)) {
                    continue;  // 事件缓存刷新
                }
                // 查找事件映射表, 找不到就将fd从epoll中删除
                auto it = event_map_.find(fd);
                if (it == event_map_.end()) {
                    epoll_ctl(event_fd_, EPOLL_CTL_DEL, fd, nullptr);
                    continue;
                }
                // 找到了，执行事件回调
                auto cb = it->second;
                try {
                    (*cb)(toPoller(ev.events));  // 执行事件回调
                } catch (std::exception& ex) {
                    ErrorL << "Exception occurred when do event task: " << ex.what();
                }
            }
        }
    } else {
        // 创建新线程执行事件循环
        loop_thread_ = new std::thread(&EventPoller::runLoop, this, true, ref_self);
        sem_run_started_.wait();  // 等待事件循环线程启动
    }
}

void EventPoller::shutdown() {
    async_l([]() { throw ExitException(); }, false, true);
    if (loop_thread_) {
        try {
            loop_thread_->join();
        } catch (...) {
            loop_thread_->detach();
        }
        delete loop_thread_;
        loop_thread_ = nullptr;
    }
}

inline void EventPoller::onPipeEvent(bool flush) {
    char buf[1024];
    int err = 0;
    if (!flush) {
        while (true) {
            if ((err = pipe_->read(buf, sizeof(buf))) > 0) {
                continue;  // 直到把管道中的数据读空为止
            }
            if (err == 0 || get_uv_error(true) != UV_EAGAIN) {
                ErrorL << "Invalid pipe fd of event poller, reopen it";
                // 管道出错，删除管道事件，重新打开管道，添加管道事件
                delEvent(pipe_->readFD());
                pipe_->reOpen();
                addEventPipe();
            }
            break;
        }
    }

    decltype(list_task_) list_swap;
    {
        std::lock_guard<decltype(mtx_task_)> lck(mtx_task_);
        list_swap.swap(list_task_);
    }

    list_swap.forEach([&](const Task::Ptr& task) {
        try {
            (*task)();
        } catch (ExitException&) {
            exit_flag_ = true;
        } catch (std::exception& ex) {
            ErrorL << "Exception occurred when do async task: " << ex.what();
        }
    });
}

uint64_t EventPoller::flushDelayTask(uint64_t now_time) {
    decltype(delay_task_map_) task_copy;
    task_copy.swap(delay_task_map_);
    // 遍历取出所有已到期的任务, 因为是有序的，遍历到第一个未到期任务退出
    for (auto it = task_copy.begin();
        it != task_copy.end() && it->first <= now_time;
        it = task_copy.erase(it)) {
        try {
            auto next_delay = (*(it->second))();
            if (next_delay) {
                delay_task_map_.emplace(next_delay + now_time, std::move(it->second));
            }
        } catch (std::exception& ex) {
            ErrorL << "Exception occurred when do delay task: " << ex.what();
        }
    }

    task_copy.insert(delay_task_map_.begin(), delay_task_map_.end());
    task_copy.swap(delay_task_map_);
    auto it = delay_task_map_.begin();
    if (it == delay_task_map_.end()) {
        return 0;
    }
    return it->first - now_time;
}

uint64_t EventPoller::getMinDelay() {
    auto it = delay_task_map_.begin();
    if (it == delay_task_map_.end()) {
        return 0;
    }
    auto now = TimeUtil::getCurrentMillisecond();
    if (it->first > now) {
        return it->first - now;  // 所有任务尚未到期
    }
    return flushDelayTask(now);  // 有任务到期，则遍历delay_task_map_执行所有到期任务
}

void EventPoller::addEventPipe() {
    SockUtil::setNoBlocked(pipe_->readFD());
    SockUtil::setNoBlocked(pipe_->writeFD());
    // 添加内部管道事件, 当管道有数据时，触发onPipeEvent()方法， 执行list_task_中的所有任务
    if (addEvent(pipe_->readFD(), Poll_Event::Read_Event, 
                [this](Poll_Event event) { onPipeEvent(false); }) == -1) {
        throw std::runtime_error("Add pipe fd to poller failed");
    }
}

//////////////////////////////// EventPollerPool /////////////////////////////

static size_t s_pool_size = 0;
static bool s_enable_cpu_affinity = true;

INSTANCE_IMP(EventPollerPool)

const std::string EventPollerPool::KOnStarted = "kBroadcastEventPollerPoolStarted";

EventPollerPool::EventPollerPool() {
    auto size = addPoller("event poller", s_pool_size, Thread_Priority::Highest, 
                          true, s_enable_cpu_affinity);
    NOTICE_EMIT(EventPollerPoolOnStartedArgs, KOnStarted, *this, size);
    InfoL << "EventPoller created size: " << size;
}

// 在调用instance()方法之前调用这两个方法
void EventPollerPool::setPoolSize(size_t size) { s_pool_size = size; }
void EventPollerPool::enableCpuAffinity(bool enable) { s_enable_cpu_affinity = enable; } 

EventPoller::Ptr EventPollerPool::getFirstPoller() {
    return std::static_pointer_cast<EventPoller>(threads_.front());
}

EventPoller::Ptr EventPollerPool::getPoller(bool prefer_current_thread) {
    auto poller = EventPoller::getCurrentPoller();
    if (prefer_current_thread && prefer_current_thread_ && poller) {
        return poller;
    }
    return std::static_pointer_cast<EventPoller>(getExecutor());
}

void EventPollerPool::preferCurrentThread(bool flag) {
    prefer_current_thread_ = flag;
}

}  // namespace xkernel


