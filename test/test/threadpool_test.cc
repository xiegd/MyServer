#include <chrono>

#include "threadpool.h"
#include "timeticker.h"
#include "logger.h"
#include "utility.h"

using namespace std;
using namespace xkernel;

int main() {
    //初始化日志系统
    Logger::Instance().add(std::make_shared<ConsoleChannel>());
    Logger::Instance().setWriter(std::make_shared<AsyncLogWriter>());

    ThreadPool pool(thread::hardware_concurrency(),
                    Thread_Priority::Highest, true);

    //每个任务耗时3秒
    auto task_second = 3;
    //每个线程平均执行4次任务，总耗时应该为12秒
    auto task_count = thread::hardware_concurrency() * 4;

    semaphore sem;  // 同步主线程和线程池中的线程
    vector<int> vec;
    vec.resize(task_count);
    Ticker ticker;
    {
        // 所有token析构后，信号量+1
        auto token =
            std::make_shared<onceToken>(nullptr, [&]() { sem.post(); });

        for (auto i = 0; i < task_count; ++i) {
            pool.async([token, i, task_second, &vec]() {
                ThreadUtil::setThreadName(("thread pool " + to_string(i)).data());
                std::this_thread::sleep_for(
                    std::chrono::seconds(task_second));  //休眠三秒
                
                InfoL << "task " << i << " done!" << ", token count: " << token.use_count();
                vec[i] = i;
            }, false);
        }
    }

    sem.wait();
    InfoL << "all task done, used milliseconds:" << ticker.elapsedTime();

    //打印执行结果
    for (auto i = 0; i < task_count; ++i) {
        InfoL << vec[i];
    }
    return 0;
}
