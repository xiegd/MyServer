/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "Timer.h"

namespace toolkit {

Timer::Timer(float second, const std::function<bool()> &cb,
             const EventPoller::Ptr &poller) {
    _poller = poller;
    if (!_poller) {
        _poller = EventPollerPool::Instance().getPoller();
    }
    // 创建定时器任务, 并设置回调函数
    _tag = _poller->doDelayTask((uint64_t)(second * 1000), [cb, second]() {
        try {
            if (cb()) {
                // 回调函数检查server是否有效
                return (uint64_t)(1000 * second);  // 返回下次执行延时，为0则不重复
            }
            return (uint64_t)0;  // 该任务不再重复  
        } catch (std::exception &ex) {
            ErrorL << "Exception occurred when do timer task: " << ex.what();
            return (uint64_t)(1000 * second);
        }
    });
}

Timer::~Timer() {
    auto tag = _tag.lock();
    if (tag) {
        tag->cancel();
    }
}

}  // namespace toolkit
