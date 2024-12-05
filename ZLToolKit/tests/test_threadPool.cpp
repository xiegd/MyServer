/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include <chrono>

#include "Thread/ThreadPool.h"
#include "Util/TimeTicker.h"
#include "Util/logger.h"
#include "Util/onceToken.h"

using namespace std;
using namespace toolkit;

int main() {
    //初始化日志系统
    Logger::Instance().add(std::make_shared<ConsoleChannel>());
    Logger::Instance().setWriter(std::make_shared<AsyncLogWriter>());

    ThreadPool pool(thread::hardware_concurrency(),
                    ThreadPool::PRIORITY_HIGHEST, true);

    //每个任务耗时3秒
    auto task_second = 3;
    //每个线程平均执行4次任务，总耗时应该为12秒
    auto task_count = thread::hardware_concurrency() * 4;

    semaphore sem;  // 同步主线程和线程池中的线程
    vector<int> vec;
    vec.resize(task_count);
    Ticker ticker;
    {
        // token是一个shared_ptr, 值传递时引用计数+1, 只有当引用计数为0时，才会被析构，
        // 然后析构过程中，就会调用这里我们在onceToken的析构中设置的sem.post()，让信号量+1
        auto token =
            std::make_shared<onceToken>(nullptr, [&]() { sem.post(); });

        for (auto i = 0; i < task_count; ++i) {
            // 值捕获token, 引用计数+1
            pool.async([token, i, task_second, &vec]() {
                setThreadName(("thread pool " + to_string(i)).data());
                std::this_thread::sleep_for(
                    std::chrono::seconds(task_second));  //休眠三秒
                InfoL << "task " << i << " done!";
                vec[i] = i;
            });
        }
    }

    sem.wait();  // 等待信号量大于0进行消费
    InfoL << "all task done, used milliseconds:" << ticker.elapsedTime();

    //打印执行结果
    for (auto i = 0; i < task_count; ++i) {
        InfoL << vec[i];
    }
    return 0;
}
