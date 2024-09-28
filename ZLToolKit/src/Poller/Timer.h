/**
 * @file Timer.h
 * @brief 定义了一个简单的定时器类
 *
 * 这个文件定义了Timer类，它提供了一种方便的方式来创建和管理定时任务。
 * Timer类使用EventPoller来调度任务，支持重复执行和自定义间隔时间。
 * 它适用于需要定期执行任务或延迟执行任务的场景。
 */

#ifndef Timer_h
#define Timer_h

#include <functional>

#include "EventPoller.h"

namespace toolkit {

/**
 * @class Timer
 * @brief 定时器类，用于创建和管理定时任务
 */
class Timer {
public:
    using Ptr = std::shared_ptr<Timer>;

    /**
     * @brief 构造定时器
     * 
     * @param second 定时器重复间隔，单位为秒
     * @param cb 定时器任务回调函数。返回true表示重复执行，false表示只执行一次。
     *           如果回调函数抛出异常，默认会重复执行下一次任务。
     * @param poller EventPoller对象，用于调度任务。可以为nullptr，此时会使用默认的EventPoller。
     */
    Timer(float second, const std::function<bool()> &cb,
          const EventPoller::Ptr &poller);

    /**
     * @brief 析构函数
     */
    ~Timer();

private:
    std::weak_ptr<EventPoller::DelayTask> _tag;
    EventPoller::Ptr _poller; ///< 定时器持有EventPoller的强引用
};

} // namespace toolkit
#endif /* Timer_h */
