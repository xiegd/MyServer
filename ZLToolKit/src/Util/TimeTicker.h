﻿/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef UTIL_TIMETICKER_H_
#define UTIL_TIMETICKER_H_

#include <cassert>
// logger.h包含了util.h, 其中有getCurrentMillisecond()等
#include "logger.h"

namespace toolkit {

/**
 * @brief 计时器类，用于代码执行时间统计和一般计时
 * 
 * Ticker 类可以用于统计代码执行时间，并在代码执行时间超过指定阈值时打印警告日志。
 */
class Ticker {
public:
    /**
     * @brief 构造函数
     * 
     * @param min_ms 代码执行时间统计的最小阈值，单位为毫秒。如果代码执行时间超过该阈值，则打印警告日志。
     * @param ctx 日志上下文捕获，用于捕获当前日志代码所在位置。
     * @param print_log 是否打印代码执行时间。
     */
    Ticker(uint64_t min_ms = 0,
           LogContextCapture ctx = LogContextCapture(Logger::Instance(), LWarn,
                                                     __FILE__, "", __LINE__),
           bool print_log = false)
        : _ctx(std::move(ctx)) {
        if (!print_log) {
            _ctx.clear();  // 清空LogContextCapture管理的LogContext的shared_ptr
        }
        _created = _begin = getCurrentMillisecond();
        _min_ms = min_ms;
    }

    /**
     * @brief 析构函数
     * 
     * 在析构时，如果代码执行时间超过指定的最小阈值，则打印警告日志。
     */
    ~Ticker() {
        uint64_t tm = createdTime();
        if (tm > _min_ms) {
            // 根据LogContextCapture中的设计，如果LogContextCapture的
            // _ctx成员是个空指针，则会直接返回*this，而不会往_ctx中写日志
            _ctx << "take time: " << tm << "ms"
                 << ", thread may be overloaded";
        } else {
            _ctx.clear();
        }
    }

    /**
     * @brief 获取上次重置计时器后至今的时间，单位为毫秒
     * 
     * @return 上次重置计时器后至今的时间，单位为毫秒
     */
    uint64_t elapsedTime() const { return getCurrentMillisecond() - _begin; }

    /**
     * @brief 获取从创建计时器至今的时间，单位为毫秒
     * 
     * @return 从创建计时器至今的时间，单位为毫秒
     */
    uint64_t createdTime() const { return getCurrentMillisecond() - _created; }

    /**
     * @brief 重置计时器
     */
    void resetTime() { _begin = getCurrentMillisecond(); }

private:
    uint64_t _min_ms;  ///< 代码执行时间统计的最小阈值，单位为毫秒
    uint64_t _begin;   ///< 上次重置计时器的时间，单位为毫秒
    uint64_t _created; ///< 创建计时器的时间，单位为毫秒
    LogContextCapture _ctx; ///< 日志上下文捕获，用于捕获当前日志代码所在位置
};

/**
 * @brief 平滑计时器类，用于生成平滑的时间戳
 * 
 * SmoothTicker 类用于生成平滑的时间戳，防止由于网络抖动导致时间戳不平滑。
 */
class SmoothTicker {
public:
    /**
     * @brief 构造函数
     * 
     * @param reset_ms 时间戳重置间隔，单位为毫秒。每隔 reset_ms 毫秒，生成的时间戳会同步一次系统时间戳。
     */
    SmoothTicker(uint64_t reset_ms = 10000) {
        _reset_ms = reset_ms;
        _ticker.resetTime();
    }

    /**
     * @brief 析构函数
     */
    ~SmoothTicker() {}

    /**
     * @brief 返回平滑的时间戳，防止由于网络抖动导致时间戳不平滑
     * 
     * @return 平滑的时间戳，单位为毫秒
     * 处理平滑时间戳和可能出现的时间回退问题
     * first_time_ 直接从ticker_获取的计时
     * last_time_ 根据first_time_和移动平均计算的time_incre_计算得到的计时
     * 二者都在进行更新，而移动平均计算的time_inc比实际的流逝时间要小
     * 所以当last_time_ > now_time(即直接从tikcer_获取的时长), 说明发生了时间回退
     * 此时需要修正time_inc_, 并重新计算last_time_
     * 同时因为time_inc_随pkt_count_增加，累加的速率会越来越小，这样就无法检查回退了
     * 所以设置了一个reset_ms_, 定期的重置first_time_, 重新开始计算
     */
    uint64_t elapsedTime() {
        auto now_time = _ticker.elapsedTime();  // 获取当前流逝的时长, 距离_begin
        // 如果是首次调用，或者进行了重置, 则重新开始计算
        if (_first_time == 0) {
            // 如果当前流逝的时长小于上一次记录的时长，说明发生时间回退，进行修正
            if (now_time < _last_time) {
                auto last_time = _last_time - _time_inc;
                double elapse_time = (now_time - last_time);
                _time_inc += (elapse_time / ++_pkt_count) / 3;
                auto ret_time = last_time + _time_inc;
                _last_time = (uint64_t)ret_time;
                return (uint64_t)ret_time;
            }
            _first_time = now_time;
            _last_time = now_time;
            _pkt_count = 0;
            _time_inc = 0;
            return now_time;
        }

        auto elapse_time = (now_time - _first_time);  //
        _time_inc += elapse_time / ++_pkt_count;
        auto ret_time = _first_time + _time_inc;
        if (elapse_time > _reset_ms) {
            _first_time = 0;
        }
        _last_time = (uint64_t)ret_time;
        return (uint64_t)ret_time;
    }

    /**
     * @brief 重置时间戳为0开始
     * 不重置_last_time和_time_inc,可以继续利用上一次的计算结果，避免时间回退
     * 因为同时在elapsedTime中进行了重置
     */
    void resetTime() {
        _first_time = 0;
        _pkt_count = 0;
        _ticker.resetTime();
    }

private:
    double _time_inc = 0; ///< 时间增量，用于平滑时间戳
    uint64_t _first_time = 0; ///< 第一次记录的时间戳
    uint64_t _last_time = 0; ///< 上一次记录的时间戳
    uint64_t _pkt_count = 0; ///< 包计数器
    uint64_t _reset_ms; ///< 时间戳重置间隔，单位为毫秒
    Ticker _ticker; ///< 内部使用的 Ticker 对象
};

// 简单情况使用#ifndef
#if !defined(NDEBUG)
// 只在debug模式下使用
/*
Ticker __ticker(5, WarnL, true)  // 允许窄化转换
Ticker __ticker{5, WarnL, true}  // 禁止窄化转换
等价于： Ticker __ticker = Ticker(5, WarnL, true)
两种方式等价，第二种编译器会优化为第一种，第一种更直观；
*/
#define TimeTicker() Ticker __ticker(5, WarnL, true)
#define TimeTicker1(tm) Ticker __ticker1(tm, WarnL, true)
#define TimeTicker2(tm, log) Ticker __ticker2(tm, log, true)
#else
// 在release模式下为空, 这里还是要定义的原因是防止出现未定义错误
#define TimeTicker()
#define TimeTicker1(tm)
#define TimeTicker2(tm, log)
#endif

} /* namespace toolkit */
#endif /* UTIL_TIMETICKER_H_ */
