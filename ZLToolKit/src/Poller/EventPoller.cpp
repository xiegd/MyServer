﻿/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "EventPoller.h"

#include "Network/sockutil.h"
#include "SelectWrap.h"
#include "Util/NoticeCenter.h"
#include "Util/TimeTicker.h"
#include "Util/util.h"
#include "Util/uv_errno.h"

// Linux平台下使用epoll
#if defined(HAS_EPOLL)
#include <sys/epoll.h>

#if !defined(EPOLLEXCLUSIVE)
#define EPOLLEXCLUSIVE 0
#endif

#define EPOLL_SIZE 1024

//防止epoll惊群
#ifndef EPOLLEXCLUSIVE
#define EPOLLEXCLUSIVE 0
#endif

// 因为这里是根据不同平台进行条件编译，所以这两个函数使用了宏定义
// 将Poll_Event枚举转换为epoll中定义的事件标志位
// 使用位运算
#define toEpoll(event)                                        \
    (((event)&Event_Read) ? EPOLLIN : 0) |                    \
        (((event)&Event_Write) ? EPOLLOUT : 0) |              \
        (((event)&Event_Error) ? (EPOLLHUP | EPOLLERR) : 0) | \
        (((event)&Event_LT) ? 0 : EPOLLET)

// 反向转换，
#define toPoller(epoll_event)                                                 \
    (((epoll_event) & (EPOLLIN | EPOLLRDNORM | EPOLLHUP)) ? Event_Read : 0) | \
        (((epoll_event) & (EPOLLOUT | EPOLLWRNORM)) ? Event_Write : 0) |      \
        (((epoll_event)&EPOLLHUP) ? Event_Error : 0) |                        \
        (((epoll_event)&EPOLLERR) ? Event_Error : 0)
#define create_event() epoll_create(EPOLL_SIZE)  // 创建epoll fd的宏，参数现在没用了，只要大于0就行，主要是为了兼容
#endif  // HAS_EPOLL

// BSD/MacOS平台下使用kqueue
#if defined(HAS_KQUEUE)
#include <sys/event.h>
#define KEVENT_SIZE 1024
#define create_event() kqueue()
#endif  // HAS_KQUEUE

using namespace std;

namespace toolkit {

EventPoller &EventPoller::Instance() {
    return *(EventPollerPool::Instance().getFirstPoller());
}

void EventPoller::addEventPipe() {
    SockUtil::setNoBlocked(_pipe.readFD());
    SockUtil::setNoBlocked(_pipe.writeFD());

    // 添加内部管道事件  
    if (addEvent(_pipe.readFD(), EventPoller::Event_Read,
                 [this](int event) { onPipeEvent(); }) == -1) {
        throw std::runtime_error("Add pipe fd to poller failed");
    }
}

EventPoller::EventPoller(std::string name) {
#if defined(HAS_EPOLL) || defined(HAS_KQUEUE)
    _event_fd = create_event();  // 使用前面的宏，创建epoll fd
    if (_event_fd == -1) {
        throw runtime_error(StrPrinter << "Create event fd failed: "
                                       << get_uv_errmsg());
    }
    SockUtil::setCloExec(_event_fd);
#endif  // HAS_EPOLL

    _name = std::move(name);
    _logger = Logger::Instance().shared_from_this();
    addEventPipe();
}

void EventPoller::shutdown() {
    async_l([]() { throw ExitException(); }, false, true);  // 添加一个会抛出ExitException的异步任务
    // 在onPipeEvent中执行任务时，会抛出ExitException，然后退出事件循环
    // 等待线程结束
    if (_loop_thread) {
        //防止作为子进程时崩溃  
        try {
            _loop_thread->join();
        } catch (...) {
            _loop_thread->detach();  // 失败则分离线程
        }
        delete _loop_thread;
        _loop_thread = nullptr;
    }
}

EventPoller::~EventPoller() {
    shutdown();

#if defined(HAS_EPOLL) || defined(HAS_KQUEUE)
    if (_event_fd != -1) {
        close(_event_fd);
        _event_fd = -1;
    }
#endif

    //退出前清理管道中的数据  
    onPipeEvent(true);
    InfoL << getThreadName();
}

// 向epoll实例中添加事件监听
int EventPoller::addEvent(int fd, int event, PollEventCB cb) {
    TimeTicker();
    if (!cb) {
        WarnL << "PollEventCB is empty";
        return -1;
    }
    // 如果当前线程就是事件轮询器线程，则直接操作epoll实例
    if (isCurrentThread()) {
#if defined(HAS_EPOLL)
        struct epoll_event ev = {0};
        ev.events = toEpoll(event);  // 事件类型
        ev.data.fd = fd;  // 用户数据的fd
        int ret = epoll_ctl(_event_fd, EPOLL_CTL_ADD, fd, &ev);  // 添加事件
        if (ret != -1) {
            // epoll只负责监听事件，不负责管理事件回调
            _event_map.emplace(fd, std::make_shared<PollEventCB>(std::move(cb)));
        }
        return ret;
#elif defined(HAS_KQUEUE)
        struct kevent kev[2];
        int index = 0;
        if (event & Event_Read) {
            EV_SET(&kev[index++], fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0,
                   nullptr);
        }
        if (event & Event_Write) {
            EV_SET(&kev[index++], fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0,
                   nullptr);
        }
        int ret = kevent(_event_fd, kev, index, nullptr, 0, nullptr);
        if (ret != -1) {
            _event_map.emplace(fd,
                               std::make_shared<PollEventCB>(std::move(cb)));
        }
        return ret;
#else  // 对于非Linux和BSD/MacOS平台，使用select
#ifndef _WIN32
        // win32平台，socket套接字不等于文件描述符，所以可能不适用这个限制
        if (fd >= FD_SETSIZE) {
            WarnL << "select() can not watch fd bigger than " << FD_SETSIZE;
            return -1;
        }
#endif
        auto record = std::make_shared<Poll_Record>();
        record->fd = fd;
        record->event = event;
        record->call_back = std::move(cb);
        _event_map.emplace(fd, record);
        return 0;
#endif
    }
    // 如果当前线程不是事件轮询器线程，则异步处理
    // 具体来说，就是把fd, event, cb封装成一个Task，然后添加到任务队列中
    // 然后往管道写数据，唤醒事件轮询器线程, 到onPipeEvent中执行遍历任务队列执行任务，即添加事件
    // mutable: 可变，表示lambda表达式可以修改捕获的变量
    // 否则这里就不能move(cb)
    async([this, fd, event, cb]() mutable {
        addEvent(fd, event, std::move(cb));
    });
    return 0;
}

int EventPoller::delEvent(int fd, PollCompleteCB cb) {
    TimeTicker();
    if (!cb) {
        cb = [](bool success) {};
    }

    if (isCurrentThread()) {
#if defined(HAS_EPOLL)
        int ret = -1;
        if (_event_map.erase(fd)) {
            _event_cache_expired.emplace(fd);  // 添加到事件缓存, 标记事件已过期
            // 防止后面执行事件回调时执行了
            ret = epoll_ctl(_event_fd, EPOLL_CTL_DEL, fd, nullptr);
        }
        cb(ret != -1);
        return ret;
#elif defined(HAS_KQUEUE)
        int ret = -1;
        if (_event_map.erase(fd)) {
            _event_cache_expired.emplace(fd);
            struct kevent kev[2];
            int index = 0;
            EV_SET(&kev[index++], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
            EV_SET(&kev[index++], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
            ret = kevent(_event_fd, kev, index, nullptr, 0, nullptr);
        }
        cb(ret != -1);
        return ret;
#else
        int ret = -1;
        if (_event_map.erase(fd)) {
            _event_cache_expired.emplace(fd);
            ret = 0;
        }
        cb(ret != -1);
        return ret;
#endif  // HAS_EPOLL
    }

    //跨线程操作  
    async([this, fd, cb]() mutable { delEvent(fd, std::move(cb)); });
    return 0;
}

int EventPoller::modifyEvent(int fd, int event, PollCompleteCB cb) {
    TimeTicker();
    if (!cb) {
        cb = [](bool success) {};
    }
    if (isCurrentThread()) {
#if defined(HAS_EPOLL)
        struct epoll_event ev = {0};
        ev.events = toEpoll(event);
        ev.data.fd = fd;
        auto ret = epoll_ctl(_event_fd, EPOLL_CTL_MOD, fd, &ev);
        cb(ret != -1);
        return ret;
#elif defined(HAS_KQUEUE)
        struct kevent kev[2];
        int index = 0;
        EV_SET(&kev[index++], fd, EVFILT_READ,
               event & Event_Read ? EV_ADD | EV_CLEAR : EV_DELETE, 0, 0,
               nullptr);
        EV_SET(&kev[index++], fd, EVFILT_WRITE,
               event & Event_Write ? EV_ADD | EV_CLEAR : EV_DELETE, 0, 0,
               nullptr);
        int ret = kevent(_event_fd, kev, index, nullptr, 0, nullptr);
        cb(ret != -1);
        return ret;
#else
        auto it = _event_map.find(fd);
        if (it != _event_map.end()) {
            it->second->event = event;
        }
        cb(it != _event_map.end());
        return it != _event_map.end() ? 0 : -1;
#endif  // HAS_EPOLL
    }
    async([this, fd, event, cb]() mutable {
        modifyEvent(fd, event, std::move(cb));
    });
    return 0;
}

// 继承自TaskExecutorInterface
Task::Ptr EventPoller::async(TaskIn task, bool may_sync) {
    return async_l(std::move(task), may_sync, false);
}

Task::Ptr EventPoller::async_first(TaskIn task, bool may_sync) {
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
        lock_guard<mutex> lck(_mtx_task);
        if (first) {
            _list_task.emplace_front(ret);
        } else {
            _list_task.emplace_back(ret);
        }
    }
    // 写数据到管道,唤醒主线程
    _pipe.write("", 1);
    return ret;
}

// 判断当前线程是否是eventpoller线程
bool EventPoller::isCurrentThread() {
    /*
    先检查事件循环线程(即_loop_thread)是否存在, 如果不存在则当前线程肯定不是eventpoller线程
    如果存在则检查_loop_thread的线程id是否和当前线程id相同
    */
    return !_loop_thread || _loop_thread->get_id() == this_thread::get_id();
}

// 处理管道事件, 在管道事件的回调函数中调用
inline void EventPoller::onPipeEvent(bool flush) {
    char buf[1024];  // 管道缓冲区, 固定大小在函数返回时自动释放
    int err = 0;
    if (!flush) {
        for (;;) {
            // 读取数据是为了清空管道，通知事件循环有新任务，数据不重要
            // 是为了触发事件，不清空管道会持续触发读事件
            if ((err = _pipe.read(buf, sizeof(buf))) > 0) {
                // 读到管道数据,继续读,直到读空为止  
                continue;
            }
            if (err == 0 || get_uv_error(true) != UV_EAGAIN) {
                // 收到eof或非EAGAIN(无更多数据)错误,说明管道无效了,重新打开管道
                ErrorL << "Invalid pipe fd of event poller, reopen it";
                // 删除当前读取完的管道对应的fd，然后重新创建管道，
                // 并将read端添加到epoll监听的事件中
                delEvent(_pipe.readFD());
                _pipe.reOpen();
                addEventPipe();
            }
            break;
        }
    }
    // 交换任务列表（避免长时间持有）
    decltype(_list_task) _list_swap;
    {
        lock_guard<mutex> lck(_mtx_task);
        _list_swap.swap(_list_task);
    }
    // 执行所有任务
    _list_swap.for_each([&](const Task::Ptr &task) {
        try {
            (*task)();
        } catch (ExitException &) {
            _exit_flag = true;
        } catch (std::exception &ex) {
            ErrorL << "Exception occurred when do async task: " << ex.what();
        }
    });
}

SocketRecvBuffer::Ptr EventPoller::getSharedBuffer(bool is_udp) {
#if !defined(__linux) && !defined(__linux__)
    // 非Linux平台下，tcp和udp共享recvfrom方案，使用同一个buffer
    is_udp = 0;
#endif
    auto ret = _shared_buffer[is_udp].lock();
    if (!ret) {
        ret = SocketRecvBuffer::create(is_udp);
        _shared_buffer[is_udp] = ret;
    }
    return ret;
}

thread::id EventPoller::getThreadId() const {
    return _loop_thread ? _loop_thread->get_id() : thread::id();
}

const std::string &EventPoller::getThreadName() const { return _name; }

// 线程局部变量，保存当前线程相关联的poller的引用, 使用weak_ptr避免循环引用
// 导致poller对象无法被析构, 同时主要是持有弱引用，别的地方析构时，
// 不会因为这里的引用计数导致poller对象无法被析构
static thread_local std::weak_ptr<EventPoller> s_current_poller;

// static
EventPoller::Ptr EventPoller::getCurrentPoller() {
    return s_current_poller.lock();
}

// blocked: 是否阻塞，ref_self: 是否引用当前poller
/*
单线程事件循环
有两类任务，延时任务和IO事件任务，延时任务在每一次事件循环后通过调用getMinDelay在flushDelayTask中执行，
IO事件任务在runLoop中的事件循环中执行
*/
void EventPoller::runLoop(bool blocked, bool ref_self) {
    if (blocked) {
        // 如果ref_self为true, 保存当前poller的引用到thread_local变量s_current_poller
        if (ref_self) {
            s_current_poller = shared_from_this();
        }
        _sem_run_started.post();  // 通知主线程，事件循环线程已启动
        _exit_flag = false;
        uint64_t minDelay;
#if defined(HAS_EPOLL)
        struct epoll_event events[EPOLL_SIZE];
        // 事件循环
        while (!_exit_flag) {
            minDelay = getMinDelay();  // 执行到期任务，返回最近延时
            startSleep();  // 统计上一段执行任务的时间 
            // 使用epoll_wait等待事件发生，超时时间由minDelay决定, 
            // 如果minDelay为0，则不设置超时时间，一直等待事件发生
            // epoll_wait返回值为发生的事件数量, 并将发生的事件写入events数组中
            int ret = epoll_wait(_event_fd, events, EPOLL_SIZE,
                                 minDelay ? minDelay : -1);
            sleepWakeUp(); // 统计sleep时间 
            if (ret <= 0) {
                //超时或被打断  
                continue;
            }

            _event_cache_expired.clear();  // 清除事件缓存
            // 遍历发生的事件, 并执行事件回调
            for (int i = 0; i < ret; ++i) {
                struct epoll_event &ev = events[i];
                int fd = ev.data.fd;
                // 如果过期事件缓存中存在该fd，则跳过, count检查fd是否存在，返回0/1
                // 其他线程执行delEvent时，会将fd从epoll中删除，并添加到过期事件缓存中
                if (_event_cache_expired.count(fd)) {
                    // event cache refresh
                    continue;
                }
                // 查找事件映射表, 找不到就将fd从epoll中删除
                auto it = _event_map.find(fd);
                if (it == _event_map.end()) {
                    epoll_ctl(_event_fd, EPOLL_CTL_DEL, fd, nullptr);
                    continue;
                }
                // 找到了，执行事件回调
                auto cb = it->second;
                try {
                    (*cb)(toPoller(ev.events));
                } catch (std::exception &ex) {
                    ErrorL << "Exception occurred when do event task: "
                           << ex.what();
                }
            }
        }
#elif defined(HAS_KQUEUE)
        struct kevent kevents[KEVENT_SIZE];
        while (!_exit_flag) {
            minDelay = getMinDelay();
            struct timespec timeout = {(long)minDelay / 1000,
                                       (long)minDelay % 1000 * 1000000};

            startSleep();
            int ret = kevent(_event_fd, nullptr, 0, kevents, KEVENT_SIZE,
                             minDelay ? &timeout : nullptr);
            sleepWakeUp();
            if (ret <= 0) {
                continue;
            }

            _event_cache_expired.clear();

            for (int i = 0; i < ret; ++i) {
                auto &kev = kevents[i];
                auto fd = kev.ident;
                if (_event_cache_expired.count(fd)) {
                    // event cache refresh
                    continue;
                }

                auto it = _event_map.find(fd);
                if (it == _event_map.end()) {
                    EV_SET(&kev, fd, kev.filter, EV_DELETE, 0, 0, nullptr);
                    kevent(_event_fd, &kev, 1, nullptr, 0, nullptr);
                    continue;
                }
                auto cb = it->second;
                int event = 0;
                switch (kev.filter) {
                    case EVFILT_READ:
                        event = Event_Read;
                        break;
                    case EVFILT_WRITE:
                        event = Event_Write;
                        break;
                    default:
                        WarnL << "unknown kevent filter: " << kev.filter;
                        break;
                }

                try {
                    (*cb)(event);
                } catch (std::exception &ex) {
                    ErrorL << "Exception occurred when do event task: "
                           << ex.what();
                }
            }
        }
#else
        int ret, max_fd;
        FdSet set_read, set_write, set_err;
        List<Poll_Record::Ptr> callback_list;
        struct timeval tv;
        while (!_exit_flag) {
            //定时器事件中可能操作_event_map  
            minDelay = getMinDelay();
            tv.tv_sec = (decltype(tv.tv_sec))(minDelay / 1000);
            tv.tv_usec = 1000 * (minDelay % 1000);

            set_read.fdZero();
            set_write.fdZero();
            set_err.fdZero();
            max_fd = 0;
            for (auto &pr : _event_map) {
                if (pr.first > max_fd) {
                    max_fd = pr.first;
                }
                if (pr.second->event & Event_Read) {
                    set_read.fdSet(pr.first);  //监听管道可读事件
                }
                if (pr.second->event & Event_Write) {
                    set_write.fdSet(pr.first);  //监听管道可写事件
                }
                if (pr.second->event & Event_Error) {
                    set_err.fdSet(pr.first);  //监听管道错误事件
                }
            }

            startSleep();  //用于统计当前线程负载情况
            ret = zl_select(max_fd + 1, &set_read, &set_write, &set_err,
                            minDelay ? &tv : nullptr);
            sleepWakeUp();  //用于统计当前线程负载情况

            if (ret <= 0) {
                //超时或被打断  
                continue;
            }

            _event_cache_expired.clear();

            //收集select事件类型  
            for (auto &pr : _event_map) {
                int event = 0;
                if (set_read.isSet(pr.first)) {
                    event |= Event_Read;
                }
                if (set_write.isSet(pr.first)) {
                    event |= Event_Write;
                }
                if (set_err.isSet(pr.first)) {
                    event |= Event_Error;
                }
                if (event != 0) {
                    pr.second->attach = event;
                    callback_list.emplace_back(pr.second);
                }
            }

            callback_list.for_each([&](Poll_Record::Ptr &record) {
                if (_event_cache_expired.count(record->fd)) {
                    // event cache refresh
                    return;
                }

                try {
                    record->call_back(record->attach);
                } catch (std::exception &ex) {
                    ErrorL << "Exception occurred when do event task: "
                           << ex.what();
                }
            });
            callback_list.clear();
        }
#endif  // HAS_EPOLL
    } else {
        _loop_thread = new thread(&EventPoller::runLoop, this, true, ref_self);
        _sem_run_started.wait();
    }
}

// 在事件循环中执行延时任务
uint64_t EventPoller::flushDelayTask(uint64_t now_time) {
    // eventpoller采用单线程事件循环，保证了操作都在同一线程中
    decltype(_delay_task_map) task_copy;
    task_copy.swap(_delay_task_map);
    // 遍历所有已到期的任务, erase会返回被删除元素的下一个元素的迭代器
    for (auto it = task_copy.begin();
         it != task_copy.end() && it->first <= now_time;
         it = task_copy.erase(it)) {
        //已到期的任务  
        try {
            // 根据延时任务的返回值决定是否重复执行任务，以及延时多久执行
            auto next_delay = (*(it->second))();  // 执行任务
            if (next_delay) {
                //可重复任务,更新时间截止线, 并添加
                _delay_task_map.emplace(next_delay + now_time,
                                        std::move(it->second));
            }
        } catch (std::exception &ex) {
            ErrorL << "Exception occurred when do delay task: " << ex.what();
        }
    }
    // 合并未到期任务和可重复任务
    task_copy.insert(_delay_task_map.begin(), _delay_task_map.end());
    task_copy.swap(_delay_task_map);  // 把剩下的任务交换回_delay_task_map

    auto it = _delay_task_map.begin();
    if (it == _delay_task_map.end()) {
        return 0;  // 延时任务执行完了，返回0
    }
    return it->first - now_time;  // 还有延时任务，返回最先需要执行的延时任务还要多长时间执行
}

uint64_t EventPoller::getMinDelay() {
    auto it = _delay_task_map.begin();
    if (it == _delay_task_map.end()) {
        //没有剩余的定时器了  
        return 0;
    }
    auto now = getCurrentMillisecond();
    if (it->first > now) {
        //所有任务尚未到期  
        return it->first - now;
    }
    //执行已到期的任务并刷新休眠延时  
    return flushDelayTask(now);
}

EventPoller::DelayTask::Ptr EventPoller::doDelayTask(
    uint64_t delay_ms, function<uint64_t()> task) {
    DelayTask::Ptr ret = std::make_shared<DelayTask>(std::move(task));
    auto time_line = getCurrentMillisecond() + delay_ms;
    async_first([time_line, ret, this]() {
        //异步执行的目的是刷新select或epoll的休眠时间 
        _delay_task_map.emplace(time_line, ret);
    });
    return ret;
}

///////////////////////////////////////////////
// 为了在EventPollerPool单例创建前配置，使用静态变量
// 单例在第一次调用instance()方法时创建，在调用之前使用setPoolSize和enableCpuAffinity方法配置
static size_t s_pool_size = 0;
static bool s_enable_cpu_affinity = true;

INSTANCE_IMP(EventPollerPool)  // 不是创建单例，而是使用宏实现instance()方法, 
// 单例靠的是instance()方法中的static的相应类型的shared_ptr实现

// 获取线程池中的第一个EventPoller
EventPoller::Ptr EventPollerPool::getFirstPoller() {
    return static_pointer_cast<EventPoller>(_threads.front());  // 强制类型转换，进行向下转型
}

// 获取线程池中负载最小的EventPoller
EventPoller::Ptr EventPollerPool::getPoller(bool prefer_current_thread) {
    auto poller = EventPoller::getCurrentPoller();
    if (prefer_current_thread && _prefer_current_thread && poller) {
        return poller;
    }
    return static_pointer_cast<EventPoller>(getExecutor());
}

void EventPollerPool::preferCurrentThread(bool flag) {
    _prefer_current_thread = flag;
}

const std::string EventPollerPool::kOnStarted =
    "kBroadcastEventPollerPoolStarted";

EventPollerPool::EventPollerPool() {
    auto size =
        addPoller("event poller", s_pool_size, ThreadPool::PRIORITY_HIGHEST,
                  true, s_enable_cpu_affinity);
    NOTICE_EMIT(EventPollerPoolOnStartedArgs, kOnStarted, *this, size);
    InfoL << "EventPoller created size: " << size;
}

void EventPollerPool::setPoolSize(size_t size) { s_pool_size = size; }

void EventPollerPool::enableCpuAffinity(bool enable) {
    s_enable_cpu_affinity = enable;
}

}  // namespace toolkit
