#ifndef _TIMER_H_
#define _TIMER_H_

#include <functional>
#include "eventpoller.h"
#include "logger.h"

namespace xkernel {

// 定时器类
class Timer {
public:
    using Ptr = std::shared_ptr<Timer>;
    Timer(float second, const std::function<bool()>& cb, const EventPoller::Ptr& poller) {
        poller_ = poller;
        if (!poller_) {
            poller_ = EventPollerPool::Instance().getPoller();
        }
        tag_ = poller_->doDelayTask(static_cast<uint64_t>(second * 1000), [cb, second](){
            try {
                if (cb()) {
                    return static_cast<uint64_t>(second * 1000);  // 重复的任务
                }
                return static_cast<uint64_t>(0);  // 该任务不再重复
            } catch (std::exception& ex) {
                ErrorL << "Exception occurred when do timer task: " << ex.what();
                return static_cast<uint64_t>(second * 1000);
            }
        });
    }

    ~Timer() {
        auto tag = tag_.lock();
        if (tag) {
            tag->cancel();
        }
    }

private:
    std::weak_ptr<EventPoller::DelayTask> tag_;
    EventPoller::Ptr poller_;
};

}  // namespace xkernel

#endif  // _TIMER_H_
