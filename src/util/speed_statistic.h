#ifndef _SPEED_STATISTIC_H_
#define _SPEED_STATISTIC_H_

#include "timeticker.h"

namespace xkernel {
// 计算数据传输实时速度的工具类
class BytesSpeed {
public:
    BytesSpeed() = default;
    ~BytesSpeed() = default;

    BytesSpeed& operator+=(size_t bytes) {
        bytes_ += bytes;
        if (bytes_ > 1024 * 1024) {
            computeSpeed();
        }
        return *this;
    }

    int getSpeed() {
        if (ticker_.elapsedTime() < 1000) {
            return speed_;
        }
        return computeSpeed();
    }

private:
    int computeSpeed() {
        auto elapsed = ticker_.elapsedTime();
        if (!elapsed) {
            return speed_;
        }
        speed_ = static_cast<int>(bytes_ * 1000 / elapsed);
        ticker_.resetTime();
        bytes_ = 0;
        return speed_;
    }

private:
    int speed_;
    size_t bytes_;
    Ticker ticker_;
};

}  // namespace xkernel

#endif