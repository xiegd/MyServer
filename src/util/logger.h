#ifndef _LOGGER_H_
#define _LOGGER_H_

#include <cstdarg>
#include <fstream>
#include <sstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

#include "utility.h"
#include "file.h"

namespace xkernel {

class LogContext;
class LogChannel;
class LogWriter;
class Logger;

using LogContextPtr = std::shared_ptr<LogContext>;

typedef enum { LTrace = 0, LDebug, LInfo, LWarn, LError } LogLevel;

Logger &getLogger();  // 获取日志单例
void setLogger(Logger *logger);  // 设置日志单例

// 日志类，单例
class Logger : public std::enable_shared_from_this<Logger>, public Noncopyable {
public:
    friend class AsyncLogWriter;
    using Ptr = std::shared_ptr<Logger>;
    // 把析构函数设置为private后，shared_ptr无法delete
    explicit Logger(const std::string &loggerName);
    ~Logger();
    static Logger &Instance();  // 获取日志单例

    void add(const std::shared_ptr<LogChannel> &channel);
    void del(const std::string &name);
    std::shared_ptr<LogChannel> get(const std::string &name);
    void setWriter(const std::shared_ptr<LogWriter> &writer);
    void setLevel(LogLevel level);  // 设置所有日志通道等级
    const std::string &getName() const;  // 获取logger名
    void write(const LogContextPtr &ctx);  // 写日志

private:
    void writeChannels(const LogContextPtr &ctx);  // 写日志到各channel，仅供AsyncLogWriter调用
    void writeChannels_l(const LogContextPtr &ctx);

private:
    LogContextPtr _last_log;
    std::string _logger_name;
    std::shared_ptr<LogWriter> _writer;
    std::shared_ptr<LogChannel> _default_channel;
    std::map<std::string, std::shared_ptr<LogChannel> > _channels;
};

// 存储单条日志的上下文信息
class LogContext : public std::ostringstream {
public:
    //_file,_function改成string保存，目的是有些情况下，指针可能会失效
    //比如说动态库中打印了一条日志，然后动态库卸载了，那么指向静态数据区的指针就会失效
    LogContext() = default;
    LogContext(LogLevel level, const char *file, const char *function, int line,
               const char *module_name, const char *flag);
    ~LogContext() = default;

    LogLevel _level;
    int _line;
    int _repeat = 0;
    std::string _file;
    std::string _function;
    std::string _thread_name;
    std::string _module_name;
    std::string _flag;
    struct timeval _tv;

    const std::string &str();

private:
    bool _got_content = false;
    std::string _content;
};


//  日志上下文捕获器
class LogContextCapture {
public:
    using Ptr = std::shared_ptr<LogContextCapture>;

    LogContextCapture(Logger &logger, LogLevel level, const char *file,
                      const char *function, int line, const char *flag = "");
    LogContextCapture(const LogContextCapture &that);
    ~LogContextCapture();

    /**
     * 输入std::endl(回车符)立即输出日志
     * @param f std::endl(回车符)
     * @return 自身引用
     */
    /**
     * 重载<<运算符，用于处理std::endl等流操纵器
     * 
     * 这个函数允许我们使用std::endl来结束一条日志消息。
     * 当传入std::endl时，它会触发日志的实际写入操作。
     * 
     * @param f 一个函数指针，通常是std::endl
     * @return 返回自身引用，允许链式调用
     */
    LogContextCapture &operator<<(std::ostream &(*f)(std::ostream &));

    template <typename T>
    LogContextCapture &operator<<(T &&data) {
        if (!_ctx) {
            return *this;
        }
        (*_ctx) << std::forward<T>(data);
        return *this;
    }

    void clear();

private:
    LogContextPtr _ctx;
    Logger &_logger;
};

// 写日志器的基类
class LogWriter : public Noncopyable {
public:
    LogWriter() = default;
    virtual ~LogWriter() = default;

    virtual void write(const LogContextPtr &ctx, Logger &logger) = 0;
};

// 异步写日志器
class AsyncLogWriter : public LogWriter {
public:
    AsyncLogWriter();
    ~AsyncLogWriter();

private:
    void run();
    void flushAll();
    void write(const LogContextPtr &ctx, Logger &logger) override;

private:
    bool _exit_flag;
    semaphore _sem;
    std::mutex _mutex;
    std::shared_ptr<std::thread> _thread;
    List<std::pair<LogContextPtr, Logger *> > _pending;
};

// 日志通道的抽象基类
class LogChannel : public Noncopyable {
public:
    LogChannel(const std::string &name, LogLevel level = LTrace);
    virtual ~LogChannel();

    virtual void write(const Logger &logger, const LogContextPtr &ctx) = 0;
    const std::string &name() const;
    void setLevel(LogLevel level);
    static std::string printTime(const timeval &tv);

protected:
    /**
    * 打印日志至输出流
    * @param ost 输出流
    * @param enable_color 是否启用颜色
    * @param enable_detail 是否打印细节(函数名、源码文件名、源码行)
    */
    virtual void format(const Logger &logger, std::ostream &ost,
                        const LogContextPtr &ctx, bool enable_color = true,
                        bool enable_detail = true);

protected:
    std::string _name;
    LogLevel _level;
};

// 输出日志到广播
class EventChannel : public LogChannel {
public:
    //输出日志时的广播名  
    static const std::string kBroadcastLogEvent;
    // xkernel目前仅只有一处全局变量被外部引用，减少导出相关定义，导出以下函数避免导出kBroadcastLogEvent
    static const std::string &getBroadcastLogEventName();
//日志广播参数类型和列表  
#define BroadcastLogEventArgs const Logger &logger, const LogContextPtr &ctx

    EventChannel(const std::string &name = "EventChannel", LogLevel level = LTrace);
    ~EventChannel() override = default;

    void write(const Logger &logger, const LogContextPtr &ctx) override;
};


// 输出日志至终端，支持输出日志至android logcat
class ConsoleChannel : public LogChannel {
public:
    ConsoleChannel(const std::string &name = "ConsoleChannel", LogLevel level = LTrace);
    ~ConsoleChannel() override = default;

    void write(const Logger &logger, const LogContextPtr &logContext) override;
};


// 输出日志至文件
class FileChannelBase : public LogChannel {
public:
    FileChannelBase(const std::string &name = "FileChannelBase",
                    const std::string &path = ExeFile::exePath() + ".log",
                    LogLevel level = LTrace);
    ~FileChannelBase() override;

    void write(const Logger &logger, const LogContextPtr &ctx) override;
    bool setPath(const std::string &path);
    const std::string &path() const;

protected:
    virtual bool open();
    virtual void close();
    virtual size_t size();

protected:
    std::string _path;
    std::ofstream _fstream;
};

class Ticker;

/**
 * 自动清理的日志文件通道
 * 默认最多保存30天的日志
 */
class FileChannel : public FileChannelBase {
public:
    FileChannel(const std::string &name = "FileChannel",
                const std::string &dir = ExeFile::exeDir() + "log/",
                LogLevel level = LTrace);
    ~FileChannel() override = default;

    void write(const Logger &logger, const LogContextPtr &ctx) override;  // 写日志(会触发新建/删除日志)
    void setMaxDay(size_t max_day);  // 设置日志最大保存天数
    void setFileMaxSize(size_t max_size);  // 设置日志切片文件最大大小
    void setFileMaxCount(size_t max_count);  // 设置日志切片文件最大个数

private:
    void clean();  // 删除日志切片文件, 根据最大保存天数和最大切片个数
    void checkSize(time_t second);  // 检查当前日志切片文件大小，如果超过限制，则创建新的日志切片文件
    void changeFile(time_t second);  // 创建并切换到下一个日志切片文件

private:
    bool _can_write = false;
    size_t _log_max_day = 30;  // 最大保存天数
    size_t _log_max_size = 128;  // 切片文件的最大大小
    size_t _log_max_count = 30;  // 最大切片个数
    size_t _index = 0;  // 当前日志切片文件索引
    int64_t _last_day = -1;
    time_t _last_check_time = 0;
    std::string _dir;
    std::set<std::string> _log_file_map;
};

#if defined(__MACH__) || \
    ((defined(__linux) || defined(__linux__)) && !defined(ANDROID))
class SysLogChannel : public LogChannel {
public:
    SysLogChannel(const std::string &name = "SysLogChannel", LogLevel level = LTrace);
    ~SysLogChannel() override = default;

    void write(const Logger &logger, const LogContextPtr &logContext) override;
};

#endif  //#if defined(__MACH__) || ((defined(__linux) || defined(__linux__)) &&
        //!defined(ANDROID))

class BaseLogFlagInterface {
protected:
    virtual ~BaseLogFlagInterface() {}
    // 获得日志标记Flag 
    const char *getLogFlag() { return _log_flag; }
    void setLogFlag(const char *flag) { _log_flag = flag; }

private:
    const char *_log_flag = "";
};

class LoggerWrapper {
public:
    template <typename First, typename... ARGS>
    static inline void printLogArray(Logger &logger, LogLevel level,
                                     const char *file, const char *function,
                                     int line, First &&first, ARGS &&...args) {
        LogContextCapture log(logger, level, file, function, line);
        log << std::forward<First>(first);
        appendLog(log, std::forward<ARGS>(args)...);
    }

    static inline void printLogArray(Logger &logger, LogLevel level,
                                     const char *file, const char *function,
                                     int line) {
        LogContextCapture log(logger, level, file, function, line);
    }

    template <typename Log, typename First, typename... ARGS>
    static inline void appendLog(Log &out, First &&first, ARGS &&...args) {
        out << std::forward<First>(first);
        appendLog(out, std::forward<ARGS>(args)...);
    }

    template <typename Log>
    static inline void appendLog(Log &out) {}

    // printf样式的日志打印 
    static void printLog(Logger &logger, int level, const char *file,
                         const char *function, int line, const char *fmt, ...);
    static void printLogV(Logger &logger, int level, const char *file,
                          const char *function, int line, const char *fmt,
                          va_list ap);
};

//可重置默认值 
extern Logger *g_defaultLogger;

//用法: DebugL << 1 << "+" << 2 << '=' << 3; 
#define WriteL(level)                                                     \
    ::xkernel::LogContextCapture(::xkernel::getLogger(), level, __FILE__, \
                                 __FUNCTION__, __LINE__)
#define TraceL WriteL(::xkernel::LTrace)
#define DebugL WriteL(::xkernel::LDebug)
#define InfoL WriteL(::xkernel::LInfo)
#define WarnL WriteL(::xkernel::LWarn)
#define ErrorL WriteL(::xkernel::LError)

//只能在虚继承BaseLogFlagInterface的类中使用 
#define WriteF(level)                                                     \
    ::xkernel::LogContextCapture(::xkernel::getLogger(), level, __FILE__, \
                                 __FUNCTION__, __LINE__, getLogFlag())
#define TraceF WriteF(::xkernel::LTrace)
#define DebugF WriteF(::xkernel::LDebug)
#define InfoF WriteF(::xkernel::LInfo)
#define WarnF WriteF(::xkernel::LWarn)
#define ErrorF WriteF(::xkernel::LError)

//用法: PrintD("%d + %s = %c", 1 "2", 'c'); 
#define PrintLog(level, ...)                                             \
    ::xkernel::LoggerWrapper::printLog(::xkernel::getLogger(), level,    \
                                       __FILE__, __FUNCTION__, __LINE__, \
                                       ##__VA_ARGS__)
#define PrintT(...) PrintLog(::xkernel::LTrace, ##__VA_ARGS__)
#define PrintD(...) PrintLog(::xkernel::LDebug, ##__VA_ARGS__)
#define PrintI(...) PrintLog(::xkernel::LInfo, ##__VA_ARGS__)
#define PrintW(...) PrintLog(::xkernel::LWarn, ##__VA_ARGS__)
#define PrintE(...) PrintLog(::xkernel::LError, ##__VA_ARGS__)

//用法: LogD(1, "+", "2", '=', 3); 
//用于模板实例化的原因，如果每次打印参数个数和类型不一致，可能会导致二进制代码膨胀
#define LogL(level, ...)                                                 \
    ::xkernel::LoggerWrapper::printLogArray(                             \
        ::xkernel::getLogger(), (LogLevel)level, __FILE__, __FUNCTION__, \
        __LINE__, ##__VA_ARGS__)
#define LogT(...) LogL(::xkernel::LTrace, ##__VA_ARGS__)
#define LogD(...) LogL(::xkernel::LDebug, ##__VA_ARGS__)
#define LogI(...) LogL(::xkernel::LInfo, ##__VA_ARGS__)
#define LogW(...) LogL(::xkernel::LWarn, ##__VA_ARGS__)
#define LogE(...) LogL(::xkernel::LError, ##__VA_ARGS__)

} /* namespace xkernel */
#endif /* UTIL_LOGGER_H_ */
