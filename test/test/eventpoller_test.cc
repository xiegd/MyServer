#include <csignal>
#include <iostream>

#include "eventpoller.h"
#include "timeticker.h"
#include "logger.h"
#include "utility.h"

using namespace std;
using namespace xkernel;

// cpu负载均衡测试
int main() {
    static bool exit_flag = false;
    // 设置信号处理函数，处理SIGINT信号，通常是ctrl+c发送的
    signal(SIGINT, [](int) { exit_flag = true; });
    //设置日志
    Logger::Instance().add(std::make_shared<ConsoleChannel>());

    Ticker ticker;
    while (!exit_flag) {
        if (ticker.elapsedTime() > 1000) {
            auto vec = EventPollerPool::Instance().getExecutorLoad();
            _StrPrinter printer;
            for (auto load : vec) {
                printer << load << "-";
            }
            DebugL << "cpu负载:" << printer;
            // 获取每个线程的任务执行延时, 并打印
            EventPollerPool::Instance().getExecutorDelay(
                [](const vector<int> &vec) {
                    _StrPrinter printer;
                    for (auto delay : vec) {
                        printer << delay << "-";
                    }
                    DebugL << "cpu任务执行延时:" << printer;
                });
            ticker.resetTime();
        }

        EventPollerPool::Instance().getExecutor()->async([]() {
            auto usec = rand() % 4000;
            // DebugL << usec;
            usleep(usec);
        }, false);

        usleep(2000);
    }

    return 0;
}

