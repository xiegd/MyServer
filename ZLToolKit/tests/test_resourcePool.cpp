﻿/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include <csignal>
#include <iostream>
#include <list>
#include <random>

#include "Thread/threadgroup.h"
#include "Util/ResourcePool.h"
#include "Util/logger.h"
#include "Util/util.h"

using namespace std;
using namespace toolkit;

//程序退出标志  [AUTO-TRANSLATED:9ac79dfb]
// Program exit flag
bool g_bExitFlag = false;

class string_imp : public string {
   public:
    template <typename... ArgTypes>
    string_imp(ArgTypes &&...args) : string(std::forward<ArgTypes>(args)...) {
        DebugL << "创建string对象:" << this << " " << *this;
    };
    ~string_imp() { WarnL << "销毁string对象:" << this << " " << *this; }
};

//后台线程任务  [AUTO-TRANSLATED:1411f16e]
// Background thread task
void onRun(ResourcePool<string_imp> &pool, int threadNum) {
    std::random_device rd;
    while (!g_bExitFlag) {
        //从循环池获取一个可用的对象  [AUTO-TRANSLATED:10783ad7]
        // Get an available object from the loop pool
        auto obj_ptr = pool.obtain();
        if (obj_ptr->empty()) {
            //这个对象是全新未使用的  [AUTO-TRANSLATED:9aaebe21]
            // This object is brand new and unused
            InfoL << "后台线程 " << threadNum << ":"
                  << "obtain a emptry object!";
        } else {
            //这个对象是循环使用的  [AUTO-TRANSLATED:c13c04d6]
            // This object is looped for reuse
            InfoL << "后台线程 " << threadNum << ":" << *obj_ptr;
        }
        //标记该对象被本线程使用  [AUTO-TRANSLATED:4730d28a]
        // Mark this object as used by the current thread
        obj_ptr->assign(StrPrinter << "keeped by thread:" << threadNum);

        //随机休眠，打乱循环使用顺序  [AUTO-TRANSLATED:313f439a]
        // Random sleep to disrupt the loop usage order
        usleep(1000 * (rd() % 10));
        obj_ptr
            .reset();  //手动释放，也可以注释这句代码。根据RAII的原理，该对象会被自动释放并重新进入循环列队
        usleep(1000 * (rd() % 1000));
    }
}

int main() {
    //初始化日志  [AUTO-TRANSLATED:371bb4e5]
    // Initialize log
    Logger::Instance().add(std::make_shared<ConsoleChannel>());
    Logger::Instance().setWriter(std::make_shared<AsyncLogWriter>());

    //大小为50的循环池  [AUTO-TRANSLATED:2ba6e2b3]
    // Loop pool of size 50
    ResourcePool<string_imp> pool;
    pool.setSize(50);

    //获取一个对象,该对象将被主线程持有，并且不会被后台线程获取并赋值
    //[AUTO-TRANSLATED:fa8654ed]
    // Get an object that will be held by the main thread and will not be
    // obtained and assigned by the background thread
    auto reservedObj = pool.obtain();
    //在主线程赋值该对象  [AUTO-TRANSLATED:a04a9b13]
    // Assign the object in the main thread
    reservedObj->assign("This is a reserved object , and will never be used!");

    thread_group group;
    //创建4个后台线程，该4个线程模拟循环池的使用场景，
    //[AUTO-TRANSLATED:a574c5f2]
    // Create 4 background threads, these 4 threads simulate the usage scenario
    // of a loop pool,
    //理论上4个线程在同一时间最多同时总共占用4个对象  [AUTO-TRANSLATED:f2a82316]
    // In theory, the 4 threads can occupy at most 4 objects at the same time

    WarnL << "主线程打印:"
          << "开始测试，主线程已经获取到的对象应该不会被后台线程获取到:"
          << *reservedObj;

    for (int i = 0; i < 4; ++i) {
        group.create_thread([i, &pool]() { onRun(pool, i); });
    }

    //等待3秒钟，此时循环池里面可用的对象基本上最少都被使用过一遍了
    //[AUTO-TRANSLATED:e5893625]
    // Wait for 3 seconds, at this time, the objects available in the loop pool
    // have been used at least once
    sleep(3);

    //但是由于reservedObj早已被主线程持有，后台线程是获取不到该对象的
    //[AUTO-TRANSLATED:2c0e93ce]
    // However, since reservedObj has been held by the main thread, the
    // background threads cannot obtain the object
    //所以其值应该尚未被覆盖  [AUTO-TRANSLATED:4cc91f7b]
    // So its value should not have been overwritten
    WarnL << "主线程打印: 该对象还在被主线程持有，其值应该保持不变:"
          << *reservedObj;

    //获取该对象的引用  [AUTO-TRANSLATED:ca198d5f]
    // Get a reference to the object
    auto &objref = *reservedObj;

    //显式释放对象,让对象重新进入循环列队，这时该对象应该会被后台线程持有并赋值
    //[AUTO-TRANSLATED:8d97a1af]
    // Explicitly release the object, allowing it to re-enter the loop queue, at
    // this time the object should be held and assigned by the background thread
    reservedObj.reset();

    WarnL << "主线程打印: 已经释放该对象,它应该会被后台线程获取到并被覆盖值";

    //再休眠3秒，让reservedObj被后台线程循环使用  [AUTO-TRANSLATED:4830df93]
    // Sleep for another 3 seconds, allowing reservedObj to be looped and used
    // by the background thread
    sleep(3);

    //这时，reservedObj还在循环池内，引用应该还是有效的，但是值应该被覆盖了
    //[AUTO-TRANSLATED:1323f66e]
    // At this time, reservedObj is still in the loop pool, the reference should
    // still be valid, but the value should have been overwritten
    WarnL << "主线程打印:对象已被后台线程赋值为:" << objref << endl;

    {
        WarnL << "主线程打印:开始测试主动放弃循环使用功能";

        List<decltype(pool)::ValuePtr> objlist;
        for (int i = 0; i < 8; ++i) {
            reservedObj = pool.obtain();
            string str = StrPrinter << i << " "
                                    << (i % 2 == 0 ? "此对象将脱离循环池管理"
                                                   : "此对象将回到循环池");
            reservedObj->assign(str);
            reservedObj.quit(i % 2 == 0);
            objlist.emplace_back(reservedObj);
        }
    }
    sleep(3);

    //通知后台线程退出  [AUTO-TRANSLATED:dbeda936]
    // Notify background thread to exit
    g_bExitFlag = true;
    //等待后台线程退出  [AUTO-TRANSLATED:49b72af6]
    // Wait for background thread to exit
    group.join_all();
    return 0;
}