#include <gtest/gtest.h>
#include <string>
#include "buffer.h"

class BufferRawTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 在每个测试用例开始前执行
    }

    void TearDown() override {
        // 在每个测试用例结束后执行
    }
};

// 用于测试的BufferRaw类, 继承自BufferRaw
class TestBufferRaw : public BufferRaw {
public:
    TestBufferRaw(size_t capacity = 0) : BufferRaw(capacity) {}
    TestBufferRaw(const char* data, size_t size = 0) : BufferRaw(data, size) {}
};

TEST_F(BufferRawTest, ConstructorAndBasicOperations) {
    TestBufferRaw buffer(10);
    EXPECT_EQ(buffer.getCapacity(), 10);
    EXPECT_EQ(buffer.size(), 0);

    buffer.setSize(5);
    EXPECT_EQ(buffer.size(), 5);

    EXPECT_THROW(buffer.setSize(11), std::invalid_argument);

    // 测试第二个重载版本的构造函数
    std::string str = "Hello, World!";
    TestBufferRaw buffer2(str.data(), str.size());
    EXPECT_EQ(buffer2.size(), str.size());
    EXPECT_EQ(buffer2.toString(), str);
}

TEST_F(BufferRawTest, AssignAndToString) {
    TestBufferRaw buffer;
    const char* testStr = "Hello, World!";
    buffer.assign(testStr);

    EXPECT_EQ(buffer.size(), strlen(testStr));
    EXPECT_EQ(buffer.toString(), std::string(testStr));
}

TEST_F(BufferRawTest, SetCapacity) {
    TestBufferRaw buffer(5);
    EXPECT_EQ(buffer.getCapacity(), 5);

    buffer.setCapacity(10);
    EXPECT_EQ(buffer.getCapacity(), 10);

    // 测试容量减小的情况
    buffer.setCapacity(8);
    EXPECT_EQ(buffer.getCapacity(), 8);
}

TEST_F(BufferRawTest, DataAccess) {
    TestBufferRaw buffer;
    const char* testStr = "Test";
    buffer.assign(testStr);

    EXPECT_STREQ(buffer.data(), testStr);
}

TEST_F(BufferRawTest, AssignWithExplicitSize) {
    TestBufferRaw buffer;
    const char* testStr = "Hello, World!";
    buffer.assign(testStr, 5);  // 只分配 "Hello"

    EXPECT_EQ(buffer.size(), 5);
    EXPECT_EQ(buffer.toString(), "Hello");
}

TEST_F(BufferRawTest, CopyConstructorAndAssignment) {
    // BufferRaw 不支持拷贝构造和赋值，所以这里不需要测试
    // 如果尝试编译以下代码，应该会失败
    // BufferRaw buffer1(10);
    // BufferRaw buffer2 = buffer1;  // 这行应该编译失败
}


class BufferLikeStringTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 在每个测试用例开始前执行的设置
    }

    void TearDown() override {
        // 在每个测试用例结束后执行的清理
    }
};

TEST_F(BufferLikeStringTest, 构造函数和赋值) {
    BufferLikeString buffer1;
    EXPECT_EQ(buffer1.size(), 0);
    EXPECT_TRUE(buffer1.empty());

    BufferLikeString buffer2("test string");
    EXPECT_EQ(buffer2.size(), 11);  // UTF-8编码的"test string"长度为11字节
    EXPECT_FALSE(buffer2.empty());

    std::string str = "test another C-style string";
    BufferLikeString buffer3(str);
    EXPECT_EQ(buffer3.size(), str.size());  // UTF-8编码的"test another string"长度为25字节

    BufferLikeString buffer4 = buffer3;
    EXPECT_EQ(buffer4.size(), buffer3.size());
    EXPECT_EQ(buffer4.toString(), buffer3.toString());

    BufferLikeString buffer5 = std::move(buffer2);
    EXPECT_EQ(buffer5.size(), 11);
    EXPECT_EQ(buffer2.size(), 0);  // 移动后原buffer应为空
}

TEST_F(BufferLikeStringTest, 数据操作) {
    BufferLikeString buffer;
    buffer.append("Hello", 5);
    EXPECT_EQ(buffer.size(), 5);
    EXPECT_EQ(buffer.toString(), "Hello");

    buffer.push_back('!');
    EXPECT_EQ(buffer.size(), 6);
    EXPECT_EQ(buffer.toString(), "Hello!");

    buffer.insert(0, "Hello, ", 7);
    EXPECT_EQ(buffer.size(), 13);
    EXPECT_EQ(buffer.toString(), "Hello, Hello!");

    buffer.append("!!");
    EXPECT_EQ(buffer.size(), 15);
    EXPECT_EQ(buffer.toString(), "Hello, Hello!!!");

    buffer.insert(12, " World", 6);
    EXPECT_EQ(buffer.size(), 21);
    EXPECT_EQ(buffer.toString(), "Hello, Hello World!!!");

    buffer.assign("new content");
    EXPECT_EQ(buffer.size(), 11);  // UTF-8编码的"new content"长度为11字节
    EXPECT_EQ(buffer.toString(), "new content");

    buffer.assign("new content", 11);
    EXPECT_EQ(buffer.size(), 11);
    EXPECT_EQ(buffer.toString(), "new content");

    buffer.assign(std::string("new content"));
    EXPECT_EQ(buffer.size(), 11);
    EXPECT_EQ(buffer.toString(), "new content");

    buffer.clear();
    EXPECT_EQ(buffer.size(), 0);
    EXPECT_TRUE(buffer.empty());
}

TEST_F(BufferLikeStringTest, 下标访问和子串) {
    std::string str = "test string";
    BufferLikeString buffer(str);
    EXPECT_EQ(buffer[0], str[0]);  // "t"的UTF-8编码首字节
    EXPECT_EQ(buffer[3], str[3]);  // "s"的UTF-8编码首字节

    EXPECT_EQ(buffer.substr(0, 6), "test s");
    EXPECT_EQ(buffer.substr(6), "tring");

    EXPECT_THROW(buffer[20], std::out_of_range);
    EXPECT_THROW(buffer.substr(20), std::out_of_range);
}

TEST_F(BufferLikeStringTest, 容量管理) {
    BufferLikeString buffer;
    buffer.reserve(100);
    EXPECT_GE(buffer.capacity(), 100);

    buffer.resize(50, 'A');
    EXPECT_EQ(buffer.size(), 50);
    EXPECT_EQ(buffer[49], 'A');

    buffer.resize(30);
    EXPECT_EQ(buffer.size(), 30);
}


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}