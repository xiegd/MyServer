#include <gtest/gtest.h>
#include "logger.h"
#include "file.h"
#include <sstream>
#include <fstream>

namespace xkernel {

// 用于捕获控制台输出的辅助类
class CaptureStream : public std::stringstream {
public:
    CaptureStream() {}
    ~CaptureStream() {}
};

// 测试夹具
class LoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 获取Logger单例
        logger = &getLogger();
    }

    void TearDown() override {
        // 清理工作
        logger->del("ConsoleChannel");
        logger->del("FileChannel");
    }

    Logger* logger;
    CaptureStream captureStream;
};

// 测试基本日志输出
TEST_F(LoggerTest, BasicLogging) {
    auto consoleChannel = std::make_shared<ConsoleChannel>();
    consoleChannel->setLevel(LDebug);
    logger->add(consoleChannel);

    testing::internal::CaptureStdout();
    DebugL << "This is a debug message";
    std::string output = testing::internal::GetCapturedStdout();

    EXPECT_TRUE(output.find("This is a debug message") != std::string::npos);
}

// 测试日志级别
TEST_F(LoggerTest, LogLevels) {
    auto consoleChannel = std::make_shared<ConsoleChannel>();
    consoleChannel->setLevel(LInfo);
    logger->add(consoleChannel);

    testing::internal::CaptureStdout();
    DebugL << "This should not appear";
    InfoL << "This should appear";
    std::string output = testing::internal::GetCapturedStdout();

    EXPECT_TRUE(output.find("This should not appear") == std::string::npos);
    EXPECT_TRUE(output.find("This should appear") != std::string::npos);
}

// 测试文件日志
TEST_F(LoggerTest, FileLogging) {
    // FileUtil::createFile("logs/test_log.txt");
    std::string testLogFile = "logs/test_log.txt";
    auto fileChannel = std::make_shared<FileChannel>("FileChannel", testLogFile);
    logger->add(fileChannel);

    InfoL << "This is a file log test";

    // 读取日志文件内容
    std::ifstream logFile(testLogFile);
    std::string fileContent((std::istreambuf_iterator<char>(logFile)),
                             std::istreambuf_iterator<char>());

    EXPECT_TRUE(fileContent.find("This is a file log test") != std::string::npos);

    // 清理测试文件
    logFile.close();
    std::remove(testLogFile.c_str());
}

// 测试异步日志
TEST_F(LoggerTest, AsyncLogging) {
    auto asyncWriter = std::make_shared<AsyncLogWriter>();
    logger->setWriter(asyncWriter);

    auto consoleChannel = std::make_shared<ConsoleChannel>();
    logger->add(consoleChannel);

    testing::internal::CaptureStdout();
    for (int i = 0; i < 100; ++i) {
        InfoL << "Async log message " << i;
    }
    
    // 等待异步日志完成
    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_TRUE(output.find("Async log message 0") != std::string::npos);
    EXPECT_TRUE(output.find("Async log message 99") != std::string::npos);
}

// 测试多通道日志
TEST_F(LoggerTest, MultipleChannels) {
    auto consoleChannel = std::make_shared<ConsoleChannel>();
    auto fileChannel = std::make_shared<FileChannel>("FileChannel", "multi_channel_test.log");
    logger->add(consoleChannel);
    logger->add(fileChannel);

    testing::internal::CaptureStdout();
    InfoL << "This should appear in both console and file";

    std::string consoleOutput = testing::internal::GetCapturedStdout();
    EXPECT_TRUE(consoleOutput.find("This should appear in both console and file") != std::string::npos);

    std::ifstream logFile("multi_channel_test.log");
    std::string fileContent((std::istreambuf_iterator<char>(logFile)),
                             std::istreambuf_iterator<char>());
    EXPECT_TRUE(fileContent.find("This should appear in both console and file") != std::string::npos);

    // 清理
    logFile.close();
    std::remove("multi_channel_test.log");
}

} // namespace xkernel

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
