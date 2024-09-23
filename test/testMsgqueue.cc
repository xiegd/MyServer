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
    TestMsg* msg = new TestMsg(42);
    queue.putMsg(msg);                    // 放入消息
    TestMsg* received = queue.getMsg();   // 获取消息
    ASSERT_NE(received, nullptr);         // 断言：确保收到的消息不为空
    EXPECT_EQ(received->value, 42);       // 断言：检查消息的值
    delete received;                      // 清理资源
}

// 测试用例：测试将消息放入队列头部的功能
TEST_F(MsgQueueTest, PutToHeadAndGet) {
    TestMsg* msg1 = new TestMsg(1);
    TestMsg* msg2 = new TestMsg(2);
    queue.putMsg(msg1);
    queue.putMsgToHead(msg2);
    
    TestMsg* received = queue.getMsg();
    ASSERT_NE(received, nullptr);
    EXPECT_EQ(received->value, 2);        // 期望先获取到放入尾部的消息
    delete received;

    received = queue.getMsg();
    ASSERT_NE(received, nullptr);
    EXPECT_EQ(received->value, 1);
    delete received;
}

// 测试用例：测试非阻塞模式
TEST_F(MsgQueueTest, NonBlockingMode) {
    queue_with_size.setNonblock();        // 设置为非阻塞模式
    for (int i = 0; i < 10; ++i) {
        queue_with_size.putMsg(new TestMsg(i));
    }
    while (queue_with_size.size() > 0) {
        TestMsg* msg = queue_with_size.getMsg();
        if (queue_with_size.size() == 0) {
            // 在非阻塞模式下，队列应该只保留前5个消息（因为队列大小为5）
            // 所以最后一个消息是4
            EXPECT_EQ(msg->value, 4);
        }
        delete msg;
    }
}

// 测试用例：测试阻塞模式下的生产者-消费者场景
TEST_F(MsgQueueTest, BlockingMode) {
    // 使用lambda表达式作为参数，捕获当前对象的this指针
    std::thread producer([this]() {
        for (int i = 0; i < 100; ++i) {
            queue_with_size.putMsg(new TestMsg(i));
            // std::cout << "putMsg: " << i << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));  // 线程sleep 10ms
        }
    });

    std::thread consumer([this]() {
        for (int i = 0; i < 100; ++i) {
            TestMsg* msg = queue_with_size.getMsg();
            // std::cout << "getMsg: " << msg->value << std::endl;
            EXPECT_EQ(msg->value, i);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));  // 线程sleep 10ms
            delete msg;
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