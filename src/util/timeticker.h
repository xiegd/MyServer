#ifndef _TIMETICKER_H_
#define _TIMETICKER_H_

#include <cassert>
#include <ctime>
#include <string>
#include "logger.h"
#include "utility.h"

namespace xkernel {
class TimeUtil {
public:
    static long getGMTOff();  // 获取相对GMT/UTC, 本地时区的偏移
    static int getDaylightActive();  // 获取当前夏令时状态
    static int isLeapYear(time_t year);  // 判断是否为闰年
    // 不使用锁，将UNIX时间戳转换为本地时间的struct tm结构
    static void noLocksLocalTime(struct tm *tmp, time_t t);
    static void localTimeInit();  // 初始化本地时间
    static uint64_t getCurrentMillisecond(bool system_time = false);  // 获取1970年至今的毫秒数
    static uint64_t getCurrentMicrosecond(bool system_time = false);  // 获取1970年至今的微秒数
    static std::string getTimeStr(const char* fmt, time_t time = 0);  // 获取格式化时间字符串
    static struct tm getLocalTime(time_t sec);  // 根据unix时间戳获取本地时间
    static inline uint64_t getCurrentMicrosecondOrigin();
    static inline bool initMillisecondThread();  // 初始化时间戳更新线程

public:
    // 流逝时间戳, 基于上次更新时间和和当前时间戳的差值计算， 不可回退 
    static std::atomic<uint64_t> s_currentMicrosecond;
    static std::atomic<uint64_t> s_currentMillisecond;
    // 系统时间戳，可能出现不连续的跳变（如系统时间被调整）, 可能回退
    static std::atomic<uint64_t> s_currentMicrosecond_system;
    static std::atomic<uint64_t> s_currentMillisecond_system;

private:
    TimeUtil() = delete;
    ~TimeUtil() = delete;
    static onceToken s_token;
    static int daylight_active_;  //是否处于夏令时 
    static long current_timezone_;  //时区偏移(s)， 表示的是本地时间相对于UTC时间的偏移量
};

// 定时器类，用于统计代码执行时间(ms)和计时
class Ticker {
public:
    Ticker(uint64_t min_ms = 0, 
           LogContextCapture ctx = LogContextCapture(Logger::Instance(), LogLevel::LWarn,
                                        __FILE__, "", __LINE__), bool print_log = false);
    ~Ticker();
public:
    uint64_t elapsedTime() const;
    uint64_t createdTime() const;
    void resetTime();
    
private:
    uint64_t min_ms_;  // 代码执行时间统计的最小阈值(ms)
    uint64_t begin_;  // 上次重置时间(ms)
    uint64_t created_;  // 创建时间(ms)
    LogContextCapture ctx_;  // 日志上下文捕获
};


class SmoothTicker {
public:
    SmoothTicker(uint64_t reset_ms = 10000);
    ~SmoothTicker();

public:
    uint64_t elapsedTime();
    void resetTime();

private:
    double time_incre_ = 0;  // 累计时间增量，用于平滑时间戳
    uint64_t first_time_ = 0;  // 直接从ticker_获取的计时
    uint64_t last_time_ = 0;  // 根据first_time_和移动平均计算的time_incre_计算得到的计时
    uint64_t pkt_count_ = 0;  // 包计数器
    uint64_t reset_ms_ = 0;  // 时间戳重置间隔(ms)
    Ticker ticker_;
};

// 在debug模式下统计代码执行时间的宏
#if !defined(NDEBUG)
#define TimeTicker() Ticker __ticker{5, WarnL, true}
#define TimeTicker1(tm) Ticker __ticker1{tm, WarnL, true}
#define TimeTicker2(tm, log) Ticker __ticker2{tm, log, true}
#else
#define TimeTicker()
#define TimeTicker1(tm)
#define TimeTicker2(tm, log)
#endif
} // namespace xkernel
#endif
