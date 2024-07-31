/**
 * @file log.h
 * @brief 日志模块封装
 * @author sylar.yin
 * @email 564628276@qq.com
 * @date 2019-05-23
 * @copyright Copyright (c) 2019年 sylar.yin All rights reserved (www.sylar.top)
 */
#ifndef __SYLAR_LOG_H__
#define __SYLAR_LOG_H__

#include <string>
#include <stdint.h>
#include <memory>
#include <list>
#include <sstream>
#include <fstream>
#include <vector>
#include <stdarg.h>
#include <map>
#include "util.h"
#include "singleton.h"
#include "thread.h"

// 使用流式方式将日志级别level的日志写入到logger
#define SYLAR_LOG_LEVEL(logger, level) \
    if(logger->getLevel() <= level) \
        sylar::LogEventWrap(sylar::LogEvent::ptr(new sylar::LogEvent(logger, level, \
                        __FILE__, __LINE__, 0, sylar::GetThreadId(),\
                sylar::GetFiberId(), time(0), sylar::Thread::GetName()))).getSS()

// 使用流式方式将日志级别debug的日志写入到logger
#define SYLAR_LOG_DEBUG(logger) SYLAR_LOG_LEVEL(logger, sylar::LogLevel::DEBUG)

// 使用流式方式将日志级别info的日志写入到logger
#define SYLAR_LOG_INFO(logger) SYLAR_LOG_LEVEL(logger, sylar::LogLevel::INFO)

// 使用流式方式将日志级别warn的日志写入到logger
#define SYLAR_LOG_WARN(logger) SYLAR_LOG_LEVEL(logger, sylar::LogLevel::WARN)

// 使用流式方式将日志级别error的日志写入到logger
#define SYLAR_LOG_ERROR(logger) SYLAR_LOG_LEVEL(logger, sylar::LogLevel::ERROR)

// 使用流式方式将日志级别fatal的日志写入到logger
#define SYLAR_LOG_FATAL(logger) SYLAR_LOG_LEVEL(logger, sylar::LogLevel::FATAL)

// 使用格式化方式将日志级别level的日志写入到logger
#define SYLAR_LOG_FMT_LEVEL(logger, level, fmt, ...) \
    if(logger->getLevel() <= level) \
        sylar::LogEventWrap(sylar::LogEvent::ptr(new sylar::LogEvent(logger, level, \
                        __FILE__, __LINE__, 0, sylar::GetThreadId(),\
                sylar::GetFiberId(), time(0), sylar::Thread::GetName()))).getEvent()->format(fmt, __VA_ARGS__)

// 使用格式化方式将日志级别debug的日志写入到logger
#define SYLAR_LOG_FMT_DEBUG(logger, fmt, ...) SYLAR_LOG_FMT_LEVEL(logger, sylar::LogLevel::DEBUG, fmt, __VA_ARGS__)

// 使用格式化方式将日志级别info的日志写入到logger
#define SYLAR_LOG_FMT_INFO(logger, fmt, ...)  SYLAR_LOG_FMT_LEVEL(logger, sylar::LogLevel::INFO, fmt, __VA_ARGS__)

// 使用格式化方式将日志级别warn的日志写入到logger
#define SYLAR_LOG_FMT_WARN(logger, fmt, ...)  SYLAR_LOG_FMT_LEVEL(logger, sylar::LogLevel::WARN, fmt, __VA_ARGS__)

// 使用格式化方式将日志级别error的日志写入到logger
#define SYLAR_LOG_FMT_ERROR(logger, fmt, ...) SYLAR_LOG_FMT_LEVEL(logger, sylar::LogLevel::ERROR, fmt, __VA_ARGS__)

// 使用格式化方式将日志级别fatal的日志写入到logger
#define SYLAR_LOG_FMT_FATAL(logger, fmt, ...) SYLAR_LOG_FMT_LEVEL(logger, sylar::LogLevel::FATAL, fmt, __VA_ARGS__)

// 获取主日志器
#define SYLAR_LOG_ROOT() sylar::LoggerMgr::GetInstance()->getRoot()

// 获取name的日志器
#define SYLAR_LOG_NAME(name) sylar::LoggerMgr::GetInstance()->getLogger(name)

namespace sylar {

class Logger;
class LoggerManager;

// 日志级别
class LogLevel {
public: 
    // 日志级别枚举
    enum Level {
        UNKNOW = 0,
        DEBUG = 1,
        INFO = 2,
        WARN = 3,
        ERROR = 4,
        FATAL = 5
    };

    static const char* ToString(LogLevel::Level level);     //将日志级别转化为文本输出
    static LogLevel::Level FromString(const std::string& str);    // 将文本转换为日志级别
};

// 日志事件, 记录日志现场，
class LogEvent {
public:
    typedef std::shared_ptr<LogEvent> ptr;
    /**
     * @brief 构造函数
     * @param[in] logger 日志器
     * @param[in] level 日志级别
     * @param[in] file 文件名
     * @param[in] line 文件行号
     * @param[in] elapse 程序启动依赖的耗时(毫秒)
     * @param[in] thread_id 线程id
     * @param[in] fiber_id 协程id
     * @param[in] time 日志事件(秒)
     * @param[in] thread_name 线程名称
     */
    LogEvent(std::shared_ptr<Logger> logger, LogLevel::Level level
            ,const char* file, int32_t line, uint32_t elapse
            ,uint32_t thread_id, uint32_t fiber_id, uint64_t time
            ,const std::string& thread_name);  // 没有初始化m_ss，根据m_logger来设置日志内容

    const char* getFile() const { return m_file;}   // 返回文件名
    int32_t getLine() const { return m_line;}  // 返回行号
    uint32_t getElapse() const { return m_elapse;}  // 返回耗时
    uint32_t getThreadId() const { return m_threadId;}  // 返回线程ID
    uint32_t getFiberId() const { return m_fiberId;}  // 返回协程ID
    uint64_t getTime() const { return m_time;}  // 返回时间
    const std::string& getThreadName() const { return m_threadName;}  // 返回线程名称
    std::string getContent() const { return m_ss.str();}  // 返回日志内容
    std::shared_ptr<Logger> getLogger() const { return m_logger;}  // 返回日志器
    LogLevel::Level getLevel() const { return m_level;}  // 返回日志级别
    std::stringstream& getSS() { return m_ss;}  // 返回日志内容字符串流
    void format(const char* fmt, ...);  // 格式化写入日志内容
    void format(const char* fmt, va_list al);  // 格式化写入日志内容

private:
    const char* m_file = nullptr;  // 文件名
    int32_t m_line = 0;  // 行号
    uint32_t m_elapse = 0;  // 程序耗时（ms）
    uint32_t m_threadId = 0;  // 线程ID
    uint32_t m_fiberId = 0;  // 协程ID
    uint64_t m_time = 0;  // 时间戳
    std::string m_threadName;  // 线程名称
    std::stringstream m_ss;  // 日志内容
    std::shared_ptr<Logger> m_logger;  // 日志器
    LogLevel::Level m_level;  // 日志等级
};


// 日志事件包装器, 将日志事件和日志器包装到一起，方便通过宏定义来简化日志模块的使用
class LogEventWrap {
public:
    LogEventWrap(LogEvent::ptr e);  // e(日志事件)
    ~LogEventWrap();  

    LogEvent::ptr getEvent() const { return m_event;}  // 获取日志事件
    std::stringstream& getSS();  // 获取日志内容流, get string stream

private:
    LogEvent::ptr m_event;  // 日志事件
};

// 日志格式化器
class LogFormatter {
public:
    typedef std::shared_ptr<LogFormatter> ptr;
    /**
     * @brief 构造函数
     * @param[in] pattern 格式模板
     * @details 
     *  %m 消息
     *  %p 日志级别
     *  %r 累计毫秒数
     *  %c 日志名称
     *  %t 线程id
     *  %n 换行
     *  %d 时间
     *  %f 文件名
     *  %l 行号
     *  %T 制表符
     *  %F 协程id
     *  %N 线程名称
     *  日志事件包含了很多内容，很多时候并不想输出所有内容，通过设置pattern来选择要输出的内容
     *  默认格式 "%d{%Y-%m-%d %H:%M:%S}%T%t%T%N%T%F%T[%p]%T[%c]%T%f:%l%T%m%n"
     */
    LogFormatter(const std::string& pattern);

    /**
     * @brief 返回格式化日志文本
     * @param[in] logger 日志器
     * @param[in] level 日志级别
     * @param[in] event 日志事件
     */
    std::string format(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event);
    std::ostream& format(std::ostream& ofs, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event);
public: 
    // 日志内容项格式化, 派生出子类对不同的日志内容项执行不同的格式化方式
    class FormatItem {
    public:
        typedef std::shared_ptr<FormatItem> ptr;
        virtual ~FormatItem() {}  // 父类的destructor声明为virtual，保证释放派生类资源时，父类的析构函数被调用
        /**
         * @brief 格式化日志到流
         * @param[in, out] os 日志输出流
         * @param[in] logger 日志器
         * @param[in] level 日志等级
         * @param[in] event 日志事件
         */
        // 下面的成员函数使用`=0`声明为纯虚函数，在派生类中必须进行实现；
        virtual void format(std::ostream& os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) = 0;
    };

    void init();  // 初始化, 解析日志模板
    bool isError() const { return m_error;}  // 是否有错误
    const std::string getPattern() const { return m_pattern;}  // 返回日志模板
private:
    std::string m_pattern;  // 日志格式模板
    std::vector<FormatItem::ptr> m_items;  // 日志格式解析后格式
    bool m_error = false;  // 是否有错误

};

// 日志输出器，用于将日志事件输出到对应的输出地, 
class LogAppender {
friend class Logger;
public:
    typedef std::shared_ptr<LogAppender> ptr;
    typedef Spinlock MutexType;

    virtual ~LogAppender() {}

    /**
     * @brief 写入日志
     * @param[in] logger 日志器
     * @param[in] level 日志级别
     * @param[in] event 日志事件
     */
    virtual void log(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) = 0;

    virtual std::string toYamlString() = 0;  // 将日志输出目标的配置转成YAML String
    void setFormatter(LogFormatter::ptr val);  // 更改日志格式器
    LogFormatter::ptr getFormatter();  // 获取日志格式器
    LogLevel::Level getLevel() const { return m_level;}  // 获取日志级别
    void setLevel(LogLevel::Level val) { m_level = val;}  // 设置日志级别
protected:
    LogLevel::Level m_level = LogLevel::DEBUG;  // 日志级别
    bool m_hasFormatter = false;  // 是否有自己的日志格式器
    MutexType m_mutex;  // Mutex
    LogFormatter::ptr m_formatter;  // 日志格式器
};

// 日志器，负责进行日志输出, 包含多个LogAppender和一个LogLevel，提供log方法，传入日志事件，判断日志级别然后输出
class Logger : public std::enable_shared_from_this<Logger> {
friend class LoggerManager;
public:
    typedef std::shared_ptr<Logger> ptr;
    typedef Spinlock MutexType;

    /**
     * @brief 构造函数
     * @param[in] name 日志器名称
     */
    Logger(const std::string& name = "root");
    void log(LogLevel::Level level, LogEvent::ptr event);  // 写日志, 需要指定LogLevel
    void debug(LogEvent::ptr event);  // 写debug级别日志, 在实现里面就是调用的前面的log，
    void info(LogEvent::ptr event);  // 写info级别日志
    void warn(LogEvent::ptr event);  // 写warn级别日志
    void error(LogEvent::ptr event);  // 写error级别日志
    void fatal(LogEvent::ptr event);  // 写fatal级别日志
    void addAppender(LogAppender::ptr appender);  // 添加日志目标
    void delAppender(LogAppender::ptr appender);  // 删除日志目标
    void clearAppenders();  // 清空日志目标
    LogLevel::Level getLevel() const { return m_level;}  // 返回日志级别
    void setLevel(LogLevel::Level val) { m_level = val;}  // 设置日志级别
    const std::string& getName() const { return m_name;}  // 返回日志名称
    void setFormatter(LogFormatter::ptr val);  // 设置日志格式器
    void setFormatter(const std::string& val);  // 设置日志格式模板, 实际调用上一个重载版本, 多了一个m_error的判断
    LogFormatter::ptr getFormatter();  // 获取日志格式器
    std::string toYamlString();  // 将日志器的配置转成YAML String

private:
    std::string m_name;  // 日志名称
    LogLevel::Level m_level;  // 日志级别
    MutexType m_mutex;  // Mutex，使用互斥锁保证线程安全
    std::list<LogAppender::ptr> m_appenders;  // 日志目标集合
    LogFormatter::ptr m_formatter;  // 日志格式器
    Logger::ptr m_root;  // 主日志器
};


// 输出到控制台的Appender
class StdoutLogAppender : public LogAppender {
public:
    typedef std::shared_ptr<StdoutLogAppender> ptr;
    void log(Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override;
    std::string toYamlString() override;
};


// 输出到文件的Appender
class FileLogAppender : public LogAppender {
public:
    typedef std::shared_ptr<FileLogAppender> ptr;
    FileLogAppender(const std::string& filename);
    void log(Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override;
    std::string toYamlString() override;
    bool reopen();  // 重新打开日志文件，成功返回true
private:
    std::string m_filename;  // 文件路径
    std::ofstream m_filestream;  // 文件流
    uint64_t m_lastTime = 0;  // 上次打开时间
};


// 日志器管理类, 单例模式，用于统一管理所有的日志器，提供日志器的创建和获取方法
class LoggerManager {
public:
    typedef Spinlock MutexType;
    LoggerManager();  // 默认初始化，设置m_root这个Logger，添加appender，将m_root添加到日志器容器m_loggers中

    Logger::ptr getLogger(const std::string& name);  // 获取日志器, name（日志器名称）, 如果没有就新建一个相应bame的Logger
    void init();  // 初始化
    Logger::ptr getRoot() const { return m_root;}  // 返回主日志器
    std::string toYamlString();  // 将所有的日志器配置转成YAML String

private:
    MutexType m_mutex;  // Mutex
    std::map<std::string, Logger::ptr> m_loggers;  // 日志器容器
    Logger::ptr m_root;  // 主日志器
};

/// 日志器管理类单例模式
typedef sylar::Singleton<LoggerManager> LoggerMgr;

}

#endif
