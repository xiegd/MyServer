//
// Created by alex on 2022/5/29.
//

/*
 * Copyright (c) 2018, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <ctime>
#ifndef _WIN32
#include <sys/time.h>
#endif

/* This is a safe version of localtime() which contains no locks and is
 * fork() friendly. Even the _r version of localtime() cannot be used safely
 * in Redis. Another thread may be calling localtime() while the main thread
 * forks(). Later when the child process calls localtime() again, for instance
 * in order to log something to the Redis log, it may deadlock: in the copy
 * of the address space of the forked process the lock will never be released.
 *
 * This function takes the timezone 'tz' as argument, and the 'dst' flag is
 * used to check if daylight saving time is currently in effect. The caller
 * of this function should obtain such information calling tzset() ASAP in the
 * main() function to obtain the timezone offset from the 'timezone' global
 * variable. To obtain the daylight information, if it is currently active or
 * not, one trick is to call localtime() in main() ASAP as well, and get the
 * information from the tm_isdst field of the tm structure. However the daylight
 * time may switch in the future for long running processes, so this information
 * should be refreshed at safe times.
 *
 * Note that this function does not work for dates < 1/1/1970, it is solely
 * designed to work with what time(NULL) may return, and to support Redis
 * logging of the dates, it's not really a complete implementation. */
namespace toolkit {

// 全局变量：存储当前是否处于夏令时
static int _daylight_active;
// 全局变量：存储当前时区偏移（秒）
static long _current_timezone;

// 获取当前夏令时状态
int get_daylight_active() { return _daylight_active; }

/**
 * 判断给定年份是否为闰年
 * @param year 要判断的年份
 * @return 1 如果是闰年，0 如果不是闰年
 */
static int is_leap_year(time_t year) {
    if (year % 4) return 0;      // 不能被4整除的年份不是闰年
    else if (year % 100) return 1; // 能被4整除但不能被100整除的是闰年
    else if (year % 400) return 0; // 能被100整除但不能被400整除的不是闰年
    else return 1;               // 能被400整除的是闰年
}

/**
 * 不使用锁的本地时间转换函数
 * 将UNIX时间戳转换为本地时间的struct tm结构
 * @param tmp 指向要填充的tm结构的指针
 * @param t UNIX时间戳
 */
void no_locks_localtime(struct tm *tmp, time_t t) {
    const time_t secs_min = 60;
    const time_t secs_hour = 3600;
    const time_t secs_day = 3600 * 24;

    // 调整时区和夏令时
    t -= _current_timezone;
    t += 3600 * get_daylight_active();
    time_t days = t / secs_day;        // 自1970年1月1日以来的天数
    time_t seconds = t % secs_day;     // 当天剩余秒数

    // 填充tm结构
    tmp->tm_isdst = get_daylight_active();
    tmp->tm_hour = seconds / secs_hour;
    tmp->tm_min = (seconds % secs_hour) / secs_min;
    tmp->tm_sec = (seconds % secs_hour) % secs_min;
#ifndef _WIN32
    tmp->tm_gmtoff = -_current_timezone;
#endif
    // 1970.01.01是星期四， 0代表星期日， 所以4代表星期四，即第一天是周四
    // 计算星期几（0 = 星期日）
    tmp->tm_wday = (days + 4) % 7;

    // 计算年份和年内天数
    tmp->tm_year = 1970;
    while (1) {
        time_t days_this_year = 365 + is_leap_year(tmp->tm_year);
        if (days_this_year > days) break;
        days -= days_this_year;
        tmp->tm_year++;
    }

    tmp->tm_yday = days;

    // 计算月份和日期
    int mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    mdays[1] += is_leap_year(tmp->tm_year);

    tmp->tm_mon = 0;
    while (days >= mdays[tmp->tm_mon]) {
        days -= mdays[tmp->tm_mon];
        tmp->tm_mon++;
    }

    tmp->tm_mday = days + 1;
    tmp->tm_year -= 1900;  // tm_year是从1900年开始计数的
}

/**
 * 初始化本地时间信息
 * 设置全局变量 _current_timezone 和 _daylight_active
 */
void local_time_init() {
    tzset();  // 初始化时区信息

#if defined(__linux__) || defined(__sun)
    _current_timezone = timezone;
#elif defined(_WIN32)
    // Windows平台特定的时区计算
    time_t time_utc;
    struct tm tm_local;

    // Get the UTC time
    time(&time_utc);

    // Get the local time
    // Use localtime_r for threads safe for linux
    // localtime_r(&time_utc, &tm_local);
    localtime_s(&tm_local, &time_utc);

    time_t time_local;
    struct tm tm_gmt;

    // Change tm to time_t
    time_local = mktime(&tm_local);

    // Change it to GMT tm
    // gmtime_r(&time_utc, &tm_gmt);//linux
    gmtime_s(&tm_gmt, &time_utc);

    int time_zone = tm_local.tm_hour - tm_gmt.tm_hour;
    if (time_zone < -12) {
        time_zone += 24;
    } else if (time_zone > 12) {
        time_zone -= 24;
    }

    _current_timezone = time_zone;
#else
    // 其他平台使用gettimeofday获取时区信息
    struct timeval tv;
    struct timezone tz;
    gettimeofday(&tv, &tz);
    _current_timezone = tz.tz_minuteswest * 60L;
#endif

    // 获取当前夏令时状态
    time_t t = time(NULL);
    struct tm *aux = localtime(&t);
    _daylight_active = aux->tm_isdst;
}

}  // namespace toolkit
