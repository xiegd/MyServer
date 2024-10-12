/*
 * 测试MsgQueue
 * g++ -std=c++11 msgqueue.cc msgqueue_test.cpp -lgtest -lgtest_main -pthread -o msgqueue_test
*/
#include "msgqueue.h"        // 包含被测试的 MsgQueue 类
#include <gtest/gtest.h>     // 包含 Google Test 框架
#include <thread>            // 用于多线程测试
#include <chrono>            // 用于时间相关操作
#include <iostream> 

// 为测试定义一个简单的消息类型
struct TestMsg {
    int value;
    explicit TestMsg(int v) : value(v) {}
};

// 定义测试夹具（Test Fixture）
// 这个类继承自 testing::Test，用于设置测试环境和提供共享的测试对象
class MsgQueueTest : public ::testing::Test {
protected:
    MsgQueue<TestMsg> queue;           // 默认大小的队列
    MsgQueue<TestMsg> queue_with_size; // 指定大小的队列

    // 构造函数：初始化测试对象
    MsgQueueTest() : queue(), queue_with_size(5) {}

    // 可以添加 SetUp() 和 TearDown() 方法来在每个测试前后执行特定操作
    // virtual void SetUp() override { ... }
    // virtual void TearDown() override { ... }
};

// 测试用例：测试基本的放入和获取消息功能
// TEST_F 宏用于创建使用测试夹具的测试
TEST_F(MsgQueueTest, PutAndGetMessage) {
    TestMsg msg{42};                    // 使用 {} 进行初始化
    queue.putMsg(msg);                  // 放入消息
    TestMsg received{0};                // 创建一个临时对象
    bool result = queue.getMsg(received);  // 获取消息
    EXPECT_TRUE(result);                // 检查是否成功获取消息
    EXPECT_EQ(received.value, 42);      // 断言：检查消息的值
}

// 测试用例：测试将消息放入队列头部的功能
TEST_F(MsgQueueTest, PutToHeadAndGet) {
    TestMsg msg1{1};                    // 使用 {} 进行初始化
    TestMsg msg2{2};                    // 使用 {} 进行初始化
    queue.putMsg(msg1);
    queue.putMsgToHead(msg2);
    
    TestMsg received{0};                // 创建一个临时对象
    bool result = queue.getMsg(received);
    EXPECT_TRUE(result);                // 检查是否成功获取消息
    EXPECT_EQ(received.value, 2);       // 期望先获取到放入头部的消息

    result = queue.getMsg(received);
    EXPECT_TRUE(result);                // 检查是否成功获取消息
    EXPECT_EQ(received.value, 1);
}

// 测试用例：测试非阻塞模式
TEST_F(MsgQueueTest, NonBlockingMode) {
    queue_with_size.setNonblock();      // 设置为非阻塞模式
    for (int i = 0; i < 10; ++i) {
        queue_with_size.putMsg(TestMsg{i}); // 使用 {} 进行初始化
    }
    TestMsg received{0};                // 创建一个临时对象
    while (queue_with_size.size() > 0) {
        bool result = queue_with_size.getMsg(received);
        EXPECT_TRUE(result);            // 检查是否成功获取消息
        if (queue_with_size.size() == 0) {
            // 在非阻塞模式下，队列应该只保留前5个消息（因为队列大小为5）
            // 所以最后一个消息是4
            EXPECT_EQ(received.value, 4);
        }
    }
}

// 测试用例：测试阻塞模式下的生产者-消费者场景
TEST_F(MsgQueueTest, BlockingMode) {
    // 使用lambda表达式作为参数，捕获当前对象的this指针
    std::thread producer([this]() {
        for (int i = 0; i < 100; ++i) {
            queue_with_size.putMsg(TestMsg{i}); // 使用 {} 进行初始化
            // std::cout << "putMsg: " << i << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(0));  // 线程sleep 10ms
        }
    });

    std::thread consumer([this]() {
        for (int i = 0; i < 100; ++i) {
            TestMsg received{0};                // 创建一个临时对象
            bool result = queue_with_size.getMsg(received);
            // std::cout << "MsgQueue size: " << queue_with_size.size() << std::endl; // 验证对Msg_size的加锁
            EXPECT_TRUE(result);                // 检查是否成功获取消息
            // std::cout << "getMsg: " << received.value << std::endl;
            EXPECT_EQ(received.value, i);
            std::this_thread::sleep_for(std::chrono::milliseconds(0));  // 线程sleep 10ms
        }
    });

    producer.join();  // 等待生产者线程完成
    consumer.join();  // 等待消费者线程完成
}

// 主函数：运行所有测试
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);  // 初始化 Google Test
    return RUN_ALL_TESTS();                  // 运行所有测试并返回结果
}