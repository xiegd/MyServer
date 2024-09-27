#ifndef _LOCALTIME_H
#define _LOCALTIME_H

#include <time.h>
#include <string>

// 获取相对GMT/UTC的时区偏移
long getGMTOff();
// 不使用锁，将UNIX时间戳转换为本地时间的struct tm结构
void noLocksLocalTime(struct tm *tmp, time_t t);
// 初始化本地时间
void localTimeInit();
// 获取当前夏令时状态
int getDaylightActive();
// 获取1970年至今的毫秒数
uint64_t getCurrentMillisecond(bool system_time = false);
// 获取1970年至今的微秒数
uint64_t getCurrentMicrosecond(bool system_time = false);
// 获取时间字符串
std::string getTimeStr(const char* fmt, time_t time = 0);
// 根据unix时间戳获取本地时间
struct tm getLocalTime(time_t sec);

static inline uint64_t getCurrentMicrosecondOrigin();

#endif