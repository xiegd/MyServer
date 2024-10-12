#include "timeticker.h"
#include <thread>
#include <sys/time.h>
#include <unistd.h>
#include <atomic>
#include <string>
#include <cstring>

#include "utility.h"
#include "logger.h"

namespace xkernel {
std::atomic<uint64_t> TimeUtil::s_currentMicrosecond{0};
std::atomic<uint64_t> TimeUtil::s_currentMillisecond{0};

std::atomic<uint64_t> TimeUtil::s_currentMicrosecond_system{getCurrentMicrosecondOrigin()};
std::atomic<uint64_t> TimeUtil::s_currentMillisecond_system{getCurrentMicrosecondOrigin() / 1000};

onceToken TimeUtil::s_token([]() -> void { localTimeInit(); });
int TimeUtil::daylight_active_{0};
long TimeUtil::current_timezone_{0};

long TimeUtil::getGMTOff() { return -current_timezone_; }
int TimeUtil::getDaylightActive() { return daylight_active_; }


int TimeUtil::isLeapYear(time_t year) {
    if (year % 4) return 0;  // 不能被4整除的年份不是闰年
    else if (year % 100) return 1;  // 能被4整除但不能被100整除的是闰年
    else if (year % 400) return 0;  // 能被100整除但不能被400整除的不是闰年
    else return 1;  // 能被400整除的是闰年
}

void TimeUtil::noLocksLocalTime(struct tm* tmp, time_t t) {
    const time_t secs_min = 60;
    const time_t secs_hour = 3600;
    const time_t secs_day = 3600 * 24;

    // 调整时区和夏令时
    t -= current_timezone_;
    t += 3600 * getDaylightActive();  // 夏令时则向前调整1h

    time_t days = t / secs_day;  // 自1970年1月1日以来的天数
    time_t seconds = t % secs_day;  // 当天剩余秒数

    tmp->tm_isdst = getDaylightActive();  // 夏令时状态
    tmp->tm_hour = seconds / secs_hour;
    tmp->tm_min = (seconds % secs_hour) / secs_min;
    tmp->tm_sec = (seconds % secs_hour) % secs_min;
    tmp->tm_gmtoff = -current_timezone_;  // gmtoff表示的是UTC时间相对于本地时间的偏移量
    tmp->tm_wday = (days + 4) % 7;  // 1970.01.01是星期四， 0代表星期日， 所以4代表星期四，即第一天是周四

    tmp->tm_year = 1970;
    while (1) {
        time_t days_this_year = 365 + isLeapYear(tmp->tm_year);
        if (days_this_year > days) break;
        days -= days_this_year;
        tmp->tm_year++;
    }
    tmp->tm_yday = days;  // 剩下不足一年的天数
    int mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    mdays[1] += isLeapYear(tmp->tm_year);

    tmp->tm_mon = 0;
    while (days >= mdays[tmp->tm_mon]) {
        days -= mdays[tmp->tm_mon];
        tmp->tm_mon++;
    }
    tmp->tm_mday = days + 1;  // days是完整经历过的天数，所以当前天数还要+1
    tmp->tm_year -= 1900;  // tm_year是从1900年开始计数的
}

void TimeUtil::localTimeInit() {
    tzset();  // 初始化时区信息
    time_t t = time(nullptr);
    struct tm* aux = localtime(&t);
    daylight_active_ = aux->tm_isdst;
    // current_timezone_ = aux->tm_gmtoff;
    current_timezone_ = timezone;
}

uint64_t TimeUtil::getCurrentMillisecond(bool system_time) {
    static bool flag = initMillisecondThread();
    // 返回系统时间戳
    if (system_time) {
        return s_currentMillisecond_system.load(std::memory_order_acquire);
    }
    return s_currentMillisecond.load(std::memory_order_acquire);  // 返回流逝时间戳
}

uint64_t TimeUtil::getCurrentMicrosecond(bool system_time) {
    static bool flag = initMillisecondThread();
    // 返回系统时间戳
    if (system_time) {
        return s_currentMicrosecond_system.load(std::memory_order_acquire);
    }
    return s_currentMicrosecond.load(std::memory_order_acquire);  // 返回流逝时间戳
}

// 获取时间字符串
std::string TimeUtil::getTimeStr(const char* fmt, time_t time) {
    if (!time) {
        time = ::time(nullptr);  // 获取当前UNIX时间戳
    } 
    auto tm = getLocalTime(time);
    size_t size = strlen(fmt) + 64;  // 预留空间防止溢出
    std::string ret;
    ret.resize(size);
    size = std::strftime(&ret[0], size, fmt, &tm);  // 返回写入的字符数
    if (size > 0) {
        ret.resize(size);  // 写入成功，调整字符串为实际大小
    } else {
        ret = fmt;  // 写入失败，返回原始格式字符串
    }
    return ret;
}

struct tm TimeUtil::getLocalTime(time_t sec) {
    struct tm tm;
    TimeUtil::noLocksLocalTime(&tm, sec);
    return tm;
}

uint64_t TimeUtil::getCurrentMicrosecondOrigin() {
    struct timeval tv;  // 两个成员: tv_sec, tv_usec表示自1970年1月1日以来的秒数和微秒数
    gettimeofday(&tv, nullptr);
    return tv.tv_sec * 1'000'000LL + tv.tv_usec;
}

bool TimeUtil::initMillisecondThread() {
    static std::thread s_thread([]() {
        ThreadUtil::setThreadName("stamp thread");
        DebugL << "Stamp thread started";
        // std::cout << "Stamp thread started" << std::endl;
        uint64_t last = getCurrentMicrosecondOrigin();
        uint64_t now;
        uint64_t microsecond = 0;
        while (true) {
            now = getCurrentMicrosecondOrigin();
            s_currentMicrosecond_system.store(now, std::memory_order_release);
            s_currentMillisecond_system.store(now / 1000, std::memory_order_release);

            int64_t expired = now - last;  // 流逝时间
            last = now;
            // 时间流逝正常(0-1s), 则更新流逝时间戳
            if (expired > 0 && expired < 1000 * 1000) {
                microsecond += expired;
                s_currentMicrosecond.store(microsecond, std::memory_order_release);
                s_currentMillisecond.store(microsecond / 1000, std::memory_order_release);
            } 
            else if (expired != 0) {
                WarnL << "Stamp expired is abnormal: " << expired;
            }
            usleep(500);
        }
    });
    // 分离线程，允许独立运行, 
    static onceToken s_token([]() { s_thread.detach(); });
    return true;
}

// Ticker::Ticker(uint64_t min_ms = 0)
} // namespace xkernel