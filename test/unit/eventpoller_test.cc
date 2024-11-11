#include <gtest/gtest.h>
#include "eventpoller.h"
#include <chrono>
#include <atomic>
#include <iostream>

using namespace xkernel;

class EventPollerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 配置 EventPollerPool
        EventPollerPool::setPoolSize(1);  // 测试时使用单个poller
        EventPollerPool::enableCpuAffinity(false);
    }
    
    void TearDown() override {
    }
};

// 测试异步任务执行
TEST_F(EventPollerTest, AsyncTask) {
    std::cout << "EventPollerPool::KOnStarted: " << EventPollerPool::KOnStarted << std::endl;
    auto poller = EventPollerPool::Instance().getFirstPoller();
    std::atomic<bool> task_executed{false};
    
    // 提交异步任务
    auto task = poller->async([&task_executed]() {
        task_executed = true;
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    // 等待任务执行完成
    EXPECT_TRUE(task_executed);
}

// 测试延时任务
TEST_F(EventPollerTest, DelayTask) {
    auto poller = EventPollerPool::Instance().getFirstPoller();
    std::atomic<bool> task_executed{false};
    
    auto start_time = std::chrono::steady_clock::now();
    
    // 创建100ms延时任务
    auto delay_task = poller->doDelayTask(100, [&]() {
        task_executed = true;
        return 0;  // 不重复执行
    });
    
    // 等待任务执行
    while (!task_executed) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();
    
    EXPECT_TRUE(task_executed);
    EXPECT_GE(duration, 100);  // 至少延时100ms
}

// 测试事件处理
TEST_F(EventPollerTest, EventHandling) {
    auto poller = EventPollerPool::Instance().getFirstPoller();
    
    // 创建一对socket用于测试
    int fds[2];
    ASSERT_EQ(pipe(fds), 0);
    
    std::atomic<bool> read_event{false};
    
    // 添加读事件监听
    EXPECT_EQ(poller->addEvent(fds[0], 
            EventPoller::Poll_Event::Read_Event, 
            [&](EventPoller::Poll_Event event) {
                read_event = true;
                char buf[1];
                read(fds[0], buf, 1);
            }), 0);
    
    // 触发读事件
    write(fds[1], "abc", 1);
    
    // 等待事件处理
    while (!read_event) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    EXPECT_TRUE(read_event);
    
    // 清理
    poller->delEvent(fds[0]);
    close(fds[0]);
    close(fds[1]);
}

// 测试线程相关功能
TEST_F(EventPollerTest, ThreadFunctions) {
    auto poller = EventPollerPool::Instance().getFirstPoller();
    
    // 测试 isCurrentThread
    std::atomic<bool> in_poller_thread{false};
    auto task = poller->async([&]() {
        in_poller_thread = poller->isCurrentThread();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_TRUE(in_poller_thread);
    
    // 测试 getCurrentPoller
    std::atomic<bool> correct_poller{false};
    task = poller->async([&]() {
        auto current = EventPoller::getCurrentPoller();
        correct_poller = (current.get() == poller.get());
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_TRUE(correct_poller);
}

// 测试事件缓存机制
TEST_F(EventPollerTest, EventCacheExpired) {
    auto poller = EventPollerPool::Instance().getFirstPoller();
    
    int fds[2];
    ASSERT_EQ(pipe(fds), 0);
    
    std::atomic<int> event_count{0};
    
    // 添加读事件监听
    EXPECT_EQ(poller->addEvent(fds[0], 
        EventPoller::Poll_Event::Read_Event,
        [&](EventPoller::Poll_Event event) {
            event_count++;
            char buf[1];
            read(fds[0], buf, 1);
        }), 0);
    
    // 触发事件并立即删除
    write(fds[1], "a", 1);
    poller->delEvent(fds[0]);
    
    // 等待一段时间
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    EXPECT_EQ(event_count, 0);  // 事件应该被缓存机制过滤
    
    // 清理
    close(fds[0]);
    close(fds[1]);
}

// 测试 EventPollerPool
TEST_F(EventPollerTest, PollerPool) {
    auto& pool = EventPollerPool::Instance();
    
    // 测试 getFirstPoller
    auto first = pool.getFirstPoller();
    EXPECT_NE(first, nullptr);
    
    // 测试 getPoller
    auto poller = pool.getPoller();
    EXPECT_NE(poller, nullptr);
    
    // 测试 preferCurrentThread
    pool.preferCurrentThread(false);
    auto poller2 = pool.getPoller(false);
    EXPECT_NE(poller2, nullptr);
}
