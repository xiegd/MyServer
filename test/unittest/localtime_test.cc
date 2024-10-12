#include <gtest/gtest.h>
#include "localtime.h"
#include <chrono>
#include <thread>
#include <ctime>

// 测试 getGMTOff 函数
TEST(LocaltimeTest, GetGMTOff) {
    long offset = getGMTOff();
    // 北京时间offset为8h
    EXPECT_EQ(offset, 8 * 3600);
    EXPECT_GE(offset, -43200);  // 最小时差 -12 小时
    EXPECT_LE(offset, 50400);   // 最大时差 +14 小时
}

// 测试 noLocksLocalTime 函数
TEST(LocaltimeTest, NoLocksLocalTime) {
    time_t now = time(nullptr);
    struct tm result;
    noLocksLocalTime(&result, now);
    
    // 验证年份在合理范围内
    EXPECT_GE(result.tm_year + 1900, 2023);
    EXPECT_LE(result.tm_year + 1900, 2100);

    // 验证月份（0-11）
    EXPECT_GE(result.tm_mon, 0);
    EXPECT_LE(result.tm_mon, 11);

    // 验证日期（1-31）
    EXPECT_GE(result.tm_mday, 1);
    EXPECT_LE(result.tm_mday, 31);

    // 验证小时（0-23）
    EXPECT_GE(result.tm_hour, 0);
    EXPECT_LE(result.tm_hour, 23);

    // 验证分钟（0-59）
    EXPECT_GE(result.tm_min, 0);
    EXPECT_LE(result.tm_min, 59);

    // 验证秒数（0-60，考虑闰秒）
    EXPECT_GE(result.tm_sec, 0);
    EXPECT_LE(result.tm_sec, 60);
}

// 测试 getCurrentMillisecond 函数
TEST(LocaltimeTest, GetCurrentMillisecond) {
    uint64_t time1 = getCurrentMillisecond();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    uint64_t time2 = getCurrentMillisecond();
    
    EXPECT_GT(time2, time1);
    EXPECT_LE(time2 - time1, 20);  // 允许有一些误差

    // 测试系统时间版本
    uint64_t sysTime1 = getCurrentMillisecond(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    uint64_t sysTime2 = getCurrentMillisecond(true);

    EXPECT_GT(sysTime2, sysTime1);
    EXPECT_LE(sysTime2 - sysTime1, 20);
}

// 测试 getTimeStr 函数
TEST(LocaltimeTest, GetTimeStr) {
    time_t testTime = 1609459200;  // 2021-01-01 08:00:00 上海时间
    std::string timeStr = getTimeStr("%Y-%m-%d %H:%M:%S", testTime);
    EXPECT_EQ(timeStr, "2021-01-01 08:00:00");
    EXPECT_EQ(timeStr.length(), 19);  // 格式应为 "YYYY-MM-DD HH:MM:SS"

    // 测试不同格式
    timeStr = getTimeStr("%Y/%m/%d", testTime);
    EXPECT_EQ(timeStr, "2021/01/01");
    EXPECT_EQ(timeStr.length(), 10);  // 格式应为 "YYYY/MM/DD"

    // 测试当前时间（不传入时间参数）
    timeStr = getTimeStr("%Y-%m-%d");
    EXPECT_EQ(timeStr, "2024-09-27");
    EXPECT_EQ(timeStr.length(), 10);  // 格式应为 "YYYY-MM-DD"
}

// 测试 getLocalTime 函数
TEST(LocaltimeTest, GetLocalTime) {
    time_t testTime = 1727444758;  // 2024-09-27 21:45:58上海时间
    struct tm localTime = getLocalTime(testTime);
    
    // 验证年份正确, tm_year是从1900年开始计数的
    EXPECT_EQ(localTime.tm_year, 2024 - 1900);
    // 验证月份正确（0-11）
    EXPECT_EQ(localTime.tm_mon + 1, 9);
    // 验证日期正确（1-31）
    EXPECT_EQ(localTime.tm_mday, 27);

    // 验证小时在合理范围内（考虑时区）
    EXPECT_EQ(localTime.tm_hour, 21);

    // 验证星期几（0-6）
    EXPECT_EQ(localTime.tm_wday, 5);

    // 验证一年中的第几天（0-365）
    EXPECT_EQ(localTime.tm_yday + 1, 271);

    // 验证夏令时标志
    EXPECT_EQ(localTime.tm_isdst, 0);
}

// 测试夏令时
TEST(LocaltimeTest, DaylightSavingTime) {
    // 系统默认没有开启夏令时
    time_t summerTime = 1625097600;  // 2021-07-01 00:00:00 UTC
    struct tm summerTm = getLocalTime(summerTime);

    // 检查夏令时标志（这可能因地区而异）
    EXPECT_EQ(summerTm.tm_isdst, 0);
}

// 主函数不是必需的，但如果您想添加一些自定义的初始化或清理代码，可以这样做：
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    // 在这里可以添加任何需要的初始化代码
    int ret = RUN_ALL_TESTS();
    // 在这里可以添加任何需要的清理代码
    return ret;
}
