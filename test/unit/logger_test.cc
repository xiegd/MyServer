#include <gtest/gtest.h>
#include "logger.h"
#include "file.h"
#include <sstream>
#include <fstream>
#include <thread>
#include "file.h"
#include "timeticker.h"

namespace xkernel {

// 用于捕获控制台输出的辅助类
class CaptureStream : public std::stringstream {
public:
    CaptureStream() {}
    ~CaptureStream() {}
};

// 测试夹具, 在每一个测试用例中独立
class LoggerTest : public ::testing::Test, public BaseLogFlagInterface {
protected:
    void SetUp() override {
        // 获取Logger单例
        logger = &getLogger();
        writer = std::make_shared<AsyncLogWriter>();
        logger->setWriter(writer);
    }

    void TearDown() override {
        // 清理工作
        // logger->del("ConsoleChannel");
        // logger->del("FileChannel");
    }

    Logger* logger;
    std::shared_ptr<AsyncLogWriter> writer;
    CaptureStream captureStream;
};

// 测试基本日志输出
TEST_F(LoggerTest, BasicLogging) {
    auto consoleChannel = std::make_shared<ConsoleChannel>("ConsoleChannel");
    consoleChannel->setLevel(xkernel::LogLevel::LTrace);
    logger->add(consoleChannel);
    setLogFlag("TestFlag");

    // 开始捕获标准输出, 本来的标准输出就不会再输出了，而是被捕获
    testing::internal::CaptureStdout();
    {
        // 打印同一级别不同风格的日志
        DebugL << "WriteL style: This is a debug message";
        // 打印日志weith flag
        DebugF << "DebugL style: This is a debug message";
        // 支持printf风格日志
        PrintD("PrintD style: %d %s", 3, "messages");
        // 支持任意个数和类型的参数
        LogD("LogD style: ", "Debug ", "messages ", "4 ");
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
    // writer->flushForTest();

    // 停止捕获标准输出并获取结果
    std::string output = testing::internal::GetCapturedStdout();

    // 验证日志输出
    EXPECT_TRUE(output.find("WriteL style: This is a debug message") != std::string::npos);
    EXPECT_TRUE(output.find("DebugL style: This is a debug message") != std::string::npos);
    EXPECT_TRUE(output.find("PrintD style: 3 messages") != std::string::npos);
    EXPECT_TRUE(output.find("LogD style: Debug messages 4") != std::string::npos);

    // 可选：打印捕获的输出，以便在测试失败时查看
    std::cout << "Captured output:\n" << output << std::endl;
    logger->del("ConsoleChannel");
}

// 测试日志级别
TEST_F(LoggerTest, LogLevels) {
    // 测试不同日志级别的日志是否按规则处理
    auto printLog = [this](LogLevel level) {
        auto consoleChannel = std::make_shared<ConsoleChannel>("LevelTestChannel");
        consoleChannel->setLevel(level);
        logger->add(consoleChannel);
        testing::internal::CaptureStdout();
        {
            TraceL << "LTrace log";
            DebugL << "LDebug log";
            InfoL << "LInfo log";
            WarnL << "LWarn log";
            ErrorL << "LError log";
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
        // writer->flushForTest();
        std::string output = testing::internal::GetCapturedStdout();
        std::cout << "Captured output: \n" << output << std::endl;
        // 看看是不是按照Channel的等级来处理相应等级的日志
        // 根据日志通道等级，捕获大于等于该等级的日志, LTrace捕获的最详细，LError捕获的最少
        switch (level) {
            case LogLevel::LTrace:
                EXPECT_TRUE(output.find("LTrace log") != std::string::npos);
                // 故意没有 break，继续执行下面的案例
            case LogLevel::LDebug:
                EXPECT_TRUE(output.find("LDebug log") != std::string::npos);
            case LogLevel::LInfo:
                EXPECT_TRUE(output.find("LInfo log") != std::string::npos);
            case LogLevel::LWarn:
                EXPECT_TRUE(output.find("LWarn log") != std::string::npos);
            case LogLevel::LError:
                EXPECT_TRUE(output.find("LError log") != std::string::npos);
                break;
            default:
                ADD_FAILURE() << "Unexpected log level";
                break;
        }
    };
    // // 只打印比大于等于channel等级的日志
    std::vector<LogLevel> levels = {LogLevel::LTrace, 
                                   LogLevel::LDebug, 
                                   LogLevel::LInfo, 
                                   LogLevel::LWarn, 
                                   LogLevel::LError};
    for (auto level : levels) {
        printLog(level);
    }
    logger->del("LevelTestChannel");
}

// 测试文件日志
TEST_F(LoggerTest, FileLogging) {
    std::string testLogPath = ExeFile::exeDir() + "test_log/";
    auto fileChannel = std::make_shared<FileChannel>("FileChannel", testLogPath);
    logger->add(fileChannel);
    {
        InfoL << "This is a file log test";
    }
    // writer->flushForTest();
    std::this_thread::sleep_for(std::chrono::seconds(1));
    // 读取日志文件内容
    std::string file_name = TimeUtil::getTimeStr("%Y-%m-%d_00.log");
    std::cout << file_name << std::endl;
    std::ifstream logFile(testLogPath + file_name);
    std::string fileContent((std::istreambuf_iterator<char>(logFile)),
                             std::istreambuf_iterator<char>());

    EXPECT_TRUE(fileContent.find("This is a file log test") != std::string::npos);
    std::cout << "FileContent: \n" << fileContent << std::endl;
    // 清理测试文件
    logFile.close();
    // FileUtil::deleteFile(testLogPath + file_name);
    logger->del("FileChannel");
    // 测试不同级别的文件日志，5个fileChannel，每个fileChannel的等级不同，看看是否能正确写入
    std::vector<std::string> log_paths = {"LTrace", "LDebug", "LInfo", "LWarn", "LError"};
    std::vector<LogLevel> levels = {LogLevel::LTrace, LogLevel::LDebug, LogLevel::LInfo, LogLevel::LWarn, LogLevel::LError};
    for (int i = 0; i < log_paths.size(); ++i) {
        auto fileChannel = std::make_shared<FileChannel>("FileChannel_" + log_paths[i], testLogPath + log_paths[i]);
        fileChannel->setLevel(levels[i]);
        logger->add(fileChannel);
        log_paths[i] = testLogPath + log_paths[i];
    }
    {
        TraceL << "LTrace log";
        DebugL << "LDebug log";
        InfoL << "LInfo log";
        WarnL << "LWarn log";
        ErrorL << "LError log";  
    }
    std::this_thread::sleep_for(std::chrono::seconds(2));
    for (int i = 0; i < log_paths.size(); ++i) {
        std::ifstream logFile(log_paths[i] + "/" + file_name);
        std::string fileContent((std::istreambuf_iterator<char>(logFile)),
                             std::istreambuf_iterator<char>());
        std::cout << log_paths[i] << " Content: \n" << fileContent << std::endl;
        switch (levels[i]) {
            case LogLevel::LTrace:
                EXPECT_TRUE(fileContent.find("LTrace log") != std::string::npos);
            case LogLevel::LDebug:
                EXPECT_TRUE(fileContent.find("LDebug log") != std::string::npos);
            case LogLevel::LInfo:
                EXPECT_TRUE(fileContent.find("LInfo log") != std::string::npos);
            case LogLevel::LWarn:
                EXPECT_TRUE(fileContent.find("LWarn log") != std::string::npos);
            case LogLevel::LError:
                EXPECT_TRUE(fileContent.find("LError log") != std::string::npos);
                break;
            default:
                ADD_FAILURE() << "Unexpected log level";
                break;
        }
    }
    std::vector<std::string> log_levels = {"LTrace", "LDebug", "LInfo", "LWarn", "LError"};
    for (int i = 0; i < log_levels.size(); ++i) {
        logger->del("FileChannel_" + log_levels[i]);
    }
    std::cout << "logger->getChannelCount() = " << logger->getChannelCount() << std::endl;
    // 调用默认日志通道
    {
        WarnL << "LWarn log test";
        ErrorL << "LError log test";
    }
    std::this_thread::sleep_for(std::chrono::seconds(2));
}

// 测试syslog
TEST_F(LoggerTest, SysLog) {
    auto syslogChannel = std::make_shared<SysLogChannel>("SysLogChannel");
    syslogChannel->setLevel(LogLevel::LTrace);
    logger->add(syslogChannel);
    {
        InfoL << "This is a syslog test";
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
    logger->del("SysLogChannel");
    // 查看syslog日志包含我们上面输出的日志
}
} // namespace xkernel

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
