#include "log.h"

#include <string.h>
#include <time.h>

#include <functional>
#include <iostream>
#include <map>

#include "config.h"
#include "env.h"
#include "macro.h"
#include "util.h"

namespace sylar {

const char* LogLevel::ToString(LogLevel::Level level) {
  switch (level) {
#define XX(name)       \
  case LogLevel::name: \
    return #name;      \
    break;

    XX(DEBUG);
    XX(INFO);
    XX(WARN);
    XX(ERROR);
    XX(FATAL);
#undef XX
    default:
      return "UNKNOW";
  }
  return "UNKNOW";
}

LogLevel::Level LogLevel::FromString(const std::string& str) {
#define XX(level, v)        \
  if (str == #v) {          \
    return LogLevel::level; \
  }  // LogLevel里并没有level，是不是写错了应该是LogLevel::Level::level
  XX(DEBUG, debug);
  XX(INFO, info);
  XX(WARN, warn);
  XX(ERROR, error);
  XX(FATAL, fatal);

  XX(DEBUG, DEBUG);
  XX(INFO, INFO);
  XX(WARN, WARN);
  XX(ERROR, ERROR);
  XX(FATAL, FATAL);
  return LogLevel::UNKNOW;
#undef XX
}

LogEventWrap::LogEventWrap(LogEvent::ptr e) : m_event(e) {}

LogEventWrap::~LogEventWrap() {
  // 将日志事件通过日志器写入到对应的appender中
  m_event->getLogger()->log(m_event->getLevel(), m_event);
}

void LogEvent::format(const char* fmt, ...) {
  va_list al;  // 存储可变参数列表信息
  va_start(
      al,
      fmt);  // 使用va_start宏初始化al，fmt告诉va_start从哪里开始获取可变参数列表
  format(fmt, al);  // 将格式化字符串和参数列表传递给，重载的format
  va_end(al);  // 使用va_end宏结束对可变参数列表的使用，释放分配的资源
}

void LogEvent::format(const char* fmt, va_list al) {
  char* buf = nullptr;
  int len = vasprintf(
      &buf, fmt,
      al);  // 将格式化字符串和可变参数列表转换为一个字符串，存放到buf中
  if (len != -1) {
    m_ss << std::string(buf, len);  // 使用string的构造函数从const
                                    // char*构造字符串，并追加到m_ss中
    free(buf);
  }
}

std::stringstream& LogEventWrap::getSS() { return m_event->getSS(); }

void LogAppender::setFormatter(LogFormatter::ptr val) {
  MutexType::Lock lock(m_mutex);
  m_formatter = val;
  if (m_formatter) {
    m_hasFormatter =
        true;  // 若设置的val不为nullptr则当前的LogAppender有日志格式器
  } else {
    m_hasFormatter = false;
  }
}

LogFormatter::ptr LogAppender::getFormatter() {
  MutexType::Lock lock(m_mutex);
  return m_formatter;
}

class MessageFormatItem : public LogFormatter::FormatItem {
 public:
  MessageFormatItem(const std::string& str = "") {
  }  // 默认使用空串进行构造；也就是默认不输出消息
  void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level,
              LogEvent::ptr event) override {
    os << event->getContent();  // 重写父类方法
  }
};

class LevelFormatItem : public LogFormatter::FormatItem {
 public:
  LevelFormatItem(const std::string& str = "") {}
  void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level,
              LogEvent::ptr event) override {
    os << LogLevel::ToString(level);
  }
};

class ElapseFormatItem : public LogFormatter::FormatItem {
 public:
  ElapseFormatItem(const std::string& str = "") {}
  void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level,
              LogEvent::ptr event) override {
    os << event->getElapse();
  }
};

class NameFormatItem : public LogFormatter::FormatItem {
 public:
  NameFormatItem(const std::string& str = "") {}
  void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level,
              LogEvent::ptr event) override {
    os << event->getLogger()->getName();
  }
};

class ThreadIdFormatItem : public LogFormatter::FormatItem {
 public:
  ThreadIdFormatItem(const std::string& str = "") {}
  void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level,
              LogEvent::ptr event) override {
    os << event->getThreadId();
  }
};

class FiberIdFormatItem : public LogFormatter::FormatItem {
 public:
  FiberIdFormatItem(const std::string& str = "") {}
  void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level,
              LogEvent::ptr event) override {
    os << event->getFiberId();
  }
};

class ThreadNameFormatItem : public LogFormatter::FormatItem {
 public:
  ThreadNameFormatItem(const std::string& str = "") {}
  void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level,
              LogEvent::ptr event) override {
    os << event->getThreadName();
  }
};

class DateTimeFormatItem : public LogFormatter::FormatItem {
 public:
  DateTimeFormatItem(const std::string& format = "%Y-%m-%d %H:%M:%S")
      : m_format(format) {
    if (m_format.empty()) {
      m_format = "%Y-%m-%d %H:%M:%S";
    }
  }

  void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level,
              LogEvent::ptr event) override {
    struct tm tm;  // time.h中定义的表示时间和日期的结构体
    time_t time =
        event->getTime();  // 获取uint64_t类型的时间戳隐式转换为time_t类型
    localtime_r(&time, &tm);  // 将时间戳转换为tm结构体
    char buf[64];             // c_str() 将string转换为char*
    strftime(buf, sizeof(buf), m_format.c_str(),
             &tm);  // 将tm结构体对应的时间按照m_format的格式转换为字符串
    os << buf;  // 将buf输出到输出流os中
  }

 private:
  std::string m_format;
};

class FilenameFormatItem : public LogFormatter::FormatItem {
 public:
  FilenameFormatItem(const std::string& str = "") {}
  void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level,
              LogEvent::ptr event) override {
    os << event->getFile();
  }
};

class LineFormatItem : public LogFormatter::FormatItem {
 public:
  LineFormatItem(const std::string& str = "") {}
  void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level,
              LogEvent::ptr event) override {
    os << event->getLine();
  }
};

class NewLineFormatItem : public LogFormatter::FormatItem {
 public:
  NewLineFormatItem(const std::string& str = "") {}
  void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level,
              LogEvent::ptr event) override {
    os << std::endl;
  }
};

class StringFormatItem : public LogFormatter::FormatItem {
 public:
  StringFormatItem(const std::string& str) : m_string(str) {}
  void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level,
              LogEvent::ptr event) override {
    os << m_string;
  }

 private:
  std::string m_string;
};

class TabFormatItem : public LogFormatter::FormatItem {
 public:
  TabFormatItem(const std::string& str = "") {}
  void format(std::ostream& os, Logger::ptr logger, LogLevel::Level level,
              LogEvent::ptr event) override {
    os << "\t";
  }

 private:
  std::string m_string;
};

LogEvent::LogEvent(std::shared_ptr<Logger> logger, LogLevel::Level level,
                   const char* file, int32_t line, uint32_t elapse,
                   uint32_t thread_id, uint32_t fiber_id, uint64_t time,
                   const std::string& thread_name)
    : m_file(file),
      m_line(line),
      m_elapse(elapse),
      m_threadId(thread_id),
      m_fiberId(fiber_id),
      m_time(time),
      m_threadName(thread_name),
      m_logger(logger),
      m_level(level) {}

Logger::Logger(const std::string& name)
    : m_name(name), m_level(LogLevel::DEBUG) {
  m_formatter.reset(new LogFormatter(
      "%d{%Y-%m-%d %H:%M:%S}%T%t%T%N%T%F%T[%p]%T[%c]%T%f:%l%T%m%n"));
}  // .reset是unique_ptr的成员函数，无参数时释放当前拥有的对象，带参数时释放当前对象，然后创建并拥有一个新对象

void Logger::setFormatter(LogFormatter::ptr val) {
  MutexType::Lock lock(m_mutex);
  m_formatter = val;
  // 对当前没有日志格式器的LogAppender设置日志格式器,
  // 为什么不修改m_hasFormatter？
  // m_hasFormatter为false，则不向对应的appender输出？默认的m_appenders包含所有的输出地，需要的输出地的m_hasFormatter为true?
  for (auto& i : m_appenders) {
    MutexType::Lock ll(i->m_mutex);
    if (!i->m_hasFormatter) {
      i->m_formatter = m_formatter;
    }
  }
}

void Logger::setFormatter(const std::string& val) {
  std::cout << "---" << val << std::endl;
  sylar::LogFormatter::ptr new_val(new sylar::LogFormatter(val));
  if (new_val->isError()) {
    std::cout << "Logger setFormatter name=" << m_name << " value=" << val
              << " invalid formatter" << std::endl;
    return;
  }
  // m_formatter = new_val;
  setFormatter(new_val);
}

std::string Logger::toYamlString() {
  MutexType::Lock lock(m_mutex);
  YAML::Node node;
  node["name"] = m_name;
  if (m_level != LogLevel::UNKNOW) {
    node["level"] = LogLevel::ToString(m_level);
  }
  if (m_formatter) {
    node["formatter"] = m_formatter->getPattern();
  }

  for (auto& i : m_appenders) {
    node["appenders"].push_back(YAML::Load(i->toYamlString()));
  }
  std::stringstream ss;
  ss << node;
  return ss.str();
}

LogFormatter::ptr Logger::getFormatter() {
  MutexType::Lock lock(m_mutex);
  return m_formatter;
}

void Logger::addAppender(LogAppender::ptr appender) {
  MutexType::Lock lock(m_mutex);
  if (!appender->getFormatter()) {
    MutexType::Lock ll(
        appender
            ->m_mutex);  // 如果添加的appender有日志格式化器，则修改为当前的Logger的m_mutex,
                         // m_formatter
    appender->m_formatter = m_formatter;
  }
  m_appenders.push_back(appender);
}

void Logger::delAppender(LogAppender::ptr appender) {
  MutexType::Lock lock(m_mutex);
  for (auto it = m_appenders.begin(); it != m_appenders.end(); ++it) {
    if (*it == appender) {
      m_appenders.erase(it);  // 从m_appenders中删除对应的appender
      break;
    }
  }
}

void Logger::clearAppenders() {
  MutexType::Lock lock(m_mutex);
  m_appenders.clear();  // 清空m_appenders
}

void Logger::log(LogLevel::Level level, LogEvent::ptr event) {
  if (level >= m_level) {
    auto self = shared_from_this();  // 创建一个指向当前对象的共享指针
    MutexType::Lock lock(m_mutex);  // 检查是否线程安全
    if (!m_appenders.empty()) {
      for (auto& i : m_appenders) {
        i->log(
            self, level,
            event);  // 向设置的那些日志输出地输出日志(LogAppender调用的log方法)
      }
    } else if (m_root) {
      m_root->log(
          level,
          event);  // 调用这个Logger对应的主日志器，主日志器应该一定设置了appender，应该不会出现递归调用吧🤣
    }
  }
}

void Logger::debug(LogEvent::ptr event) { log(LogLevel::DEBUG, event); }

void Logger::info(LogEvent::ptr event) { log(LogLevel::INFO, event); }

void Logger::warn(LogEvent::ptr event) { log(LogLevel::WARN, event); }

void Logger::error(LogEvent::ptr event) { log(LogLevel::ERROR, event); }

void Logger::fatal(LogEvent::ptr event) { log(LogLevel::FATAL, event); }

FileLogAppender::FileLogAppender(const std::string& filename)
    : m_filename(filename) {
  reopen();
}

void FileLogAppender::log(std::shared_ptr<Logger> logger, LogLevel::Level level,
                          LogEvent::ptr event) {
  if (level >= m_level) {
    uint64_t now = event->getTime();
    if (now >= (m_lastTime + 3)) {
      reopen();          // 为什么要 +3？
      m_lastTime = now;  // 更新最近打开时间
    }
    MutexType::Lock lock(m_mutex);
    // if(!(m_filestream << m_formatter->format(logger, level, event))) {
    if (!m_formatter->format(m_filestream, logger, level, event)) {
      std::cout << "error" << std::endl;
    }
  }
}

std::string FileLogAppender::toYamlString() {
  MutexType::Lock lock(m_mutex);
  YAML::Node node;
  node["type"] = "FileLogAppender";
  node["file"] = m_filename;
  if (m_level != LogLevel::UNKNOW) {
    node["level"] = LogLevel::ToString(m_level);
  }
  if (m_hasFormatter && m_formatter) {
    node["formatter"] = m_formatter->getPattern();
  }
  std::stringstream ss;
  ss << node;
  return ss.str();
}

bool FileLogAppender::reopen() {
  MutexType::Lock lock(m_mutex);
  if (m_filestream) {
    m_filestream.close();
  }
  return FSUtil::OpenForWrite(m_filestream, m_filename, std::ios::app);
}

void StdoutLogAppender::log(std::shared_ptr<Logger> logger,
                            LogLevel::Level level, LogEvent::ptr event) {
  if (level >= m_level) {
    MutexType::Lock lock(m_mutex);
    m_formatter->format(std::cout, logger, level, event);
  }
}

std::string StdoutLogAppender::toYamlString() {
  MutexType::Lock lock(m_mutex);
  YAML::Node node;
  node["type"] = "StdoutLogAppender";
  if (m_level != LogLevel::UNKNOW) {
    node["level"] = LogLevel::ToString(m_level);
  }
  if (m_hasFormatter && m_formatter) {
    node["formatter"] = m_formatter->getPattern();
  }
  std::stringstream ss;
  ss << node;
  return ss.str();
}

LogFormatter::LogFormatter(const std::string& pattern) : m_pattern(pattern) {
  init();
}

std::string LogFormatter::format(std::shared_ptr<Logger> logger,
                                 LogLevel::Level level, LogEvent::ptr event) {
  std::stringstream ss;
  for (auto& i : m_items) {
    i->format(ss, logger, level, event);
  }
  return ss.str();
}

std::ostream& LogFormatter::format(std::ostream& ofs,
                                   std::shared_ptr<Logger> logger,
                                   LogLevel::Level level, LogEvent::ptr event) {
  for (auto& i : m_items) {
    i->format(ofs, logger, level, event);
  }
  return ofs;
}

//%xxx %xxx{xxx} %%, 解析输入的格式化串
void LogFormatter::init() {
  // str, format, type
  std::vector<std::tuple<std::string, std::string, int> > vec;
  std::string nstr;
  for (size_t i = 0; i < m_pattern.size(); ++i) {
    if (m_pattern[i] != '%') {
      nstr.append(1, m_pattern[i]);
      continue;
    }

    if ((i + 1) < m_pattern.size()) {
      if (m_pattern[i + 1] == '%') {
        nstr.append(1, '%');
        continue;
      }
    }

    size_t n = i + 1;
    int fmt_status = 0;
    size_t fmt_begin = 0;

    std::string str;
    std::string fmt;
    while (n < m_pattern.size()) {
      if (!fmt_status && (!isalpha(m_pattern[n]) && m_pattern[n] != '{' &&
                          m_pattern[n] != '}')) {
        str = m_pattern.substr(i + 1, n - i - 1);
        break;
      }
      if (fmt_status == 0) {
        if (m_pattern[n] == '{') {
          str = m_pattern.substr(i + 1, n - i - 1);
          // std::cout << "*" << str << std::endl;
          fmt_status = 1;  //解析格式
          fmt_begin = n;
          ++n;
          continue;
        }
      } else if (fmt_status == 1) {
        if (m_pattern[n] == '}') {
          fmt = m_pattern.substr(fmt_begin + 1, n - fmt_begin - 1);
          // std::cout << "#" << fmt << std::endl;
          fmt_status = 0;
          ++n;
          break;
        }
      }
      ++n;
      if (n == m_pattern.size()) {
        if (str.empty()) {
          str = m_pattern.substr(i + 1);
        }
      }
    }

    if (fmt_status == 0) {
      if (!nstr.empty()) {
        vec.push_back(std::make_tuple(nstr, std::string(), 0));
        nstr.clear();
      }
      vec.push_back(std::make_tuple(str, fmt, 1));
      i = n - 1;
    } else if (fmt_status == 1) {
      std::cout << "pattern parse error: " << m_pattern << " - "
                << m_pattern.substr(i) << std::endl;
      m_error = true;
      vec.push_back(std::make_tuple("<<pattern_error>>", fmt, 0));
    }
  }

  if (!nstr.empty()) {
    vec.push_back(std::make_tuple(nstr, "", 0));
  }
  static std::map<std::string,
                  std::function<FormatItem::ptr(const std::string& str)> >
      s_format_items = {
#define XX(str, C)                                                           \
  {                                                                          \
#str, [](const std::string& fmt) { return FormatItem::ptr(new C(fmt)); } \
  }

          XX(m, MessageFormatItem),     // m:消息
          XX(p, LevelFormatItem),       // p:日志级别
          XX(r, ElapseFormatItem),      // r:累计毫秒数
          XX(c, NameFormatItem),        // c:日志名称
          XX(t, ThreadIdFormatItem),    // t:线程id
          XX(n, NewLineFormatItem),     // n:换行
          XX(d, DateTimeFormatItem),    // d:时间
          XX(f, FilenameFormatItem),    // f:文件名
          XX(l, LineFormatItem),        // l:行号
          XX(T, TabFormatItem),         // T:Tab
          XX(F, FiberIdFormatItem),     // F:协程id
          XX(N, ThreadNameFormatItem),  // N:线程名称
#undef XX
      };

  for (auto& i : vec) {
    if (std::get<2>(i) == 0) {
      m_items.push_back(FormatItem::ptr(new StringFormatItem(std::get<0>(i))));
    } else {
      auto it = s_format_items.find(std::get<0>(i));
      if (it == s_format_items.end()) {
        m_items.push_back(FormatItem::ptr(
            new StringFormatItem("<<error_format %" + std::get<0>(i) + ">>")));
        m_error = true;
      } else {
        m_items.push_back(it->second(std::get<1>(i)));
      }
    }

    // std::cout << "(" << std::get<0>(i) << ") - (" << std::get<1>(i) << ")
    // - (" << std::get<2>(i) << ")" << std::endl;
  }
  // std::cout << m_items.size() << std::endl;
}

LoggerManager::LoggerManager() {
  m_root.reset(new Logger);
  m_root->addAppender(LogAppender::ptr(new StdoutLogAppender));

  m_loggers[m_root->m_name] = m_root;

  init();
}

Logger::ptr LoggerManager::getLogger(const std::string& name) {
  MutexType::Lock lock(m_mutex);
  auto it = m_loggers.find(name);
  if (it != m_loggers.end()) {
    return it->second;
  }

  Logger::ptr logger(new Logger(name));
  logger->m_root = m_root;
  m_loggers[name] = logger;
  return logger;
}

struct LogAppenderDefine {
  int type = 0;  // 1 File, 2 Stdout
  LogLevel::Level level = LogLevel::UNKNOW;
  std::string formatter;
  std::string file;

  bool operator==(const LogAppenderDefine& oth) const {
    return type == oth.type && level == oth.level &&
           formatter == oth.formatter && file == oth.file;
  }
};

struct LogDefine {
  std::string name;
  LogLevel::Level level = LogLevel::UNKNOW;
  std::string formatter;
  std::vector<LogAppenderDefine> appenders;

  bool operator==(const LogDefine& oth) const {
    return name == oth.name && level == oth.level &&
           formatter == oth.formatter && appenders == appenders;
  }

  bool operator<(const LogDefine& oth) const { return name < oth.name; }

  bool isValid() const { return !name.empty(); }
};

template <>
class LexicalCast<std::string, LogDefine> {
 public:
  LogDefine operator()(const std::string& v) {
    YAML::Node n = YAML::Load(v);
    LogDefine ld;
    if (!n["name"].IsDefined()) {
      std::cout << "log config error: name is null, " << n << std::endl;
      throw std::logic_error("log config name is null");
    }
    ld.name = n["name"].as<std::string>();
    ld.level = LogLevel::FromString(
        n["level"].IsDefined() ? n["level"].as<std::string>() : "");
    if (n["formatter"].IsDefined()) {
      ld.formatter = n["formatter"].as<std::string>();
    }

    if (n["appenders"].IsDefined()) {
      // std::cout << "==" << ld.name << " = " << n["appenders"].size() <<
      // std::endl;
      for (size_t x = 0; x < n["appenders"].size(); ++x) {
        auto a = n["appenders"][x];
        if (!a["type"].IsDefined()) {
          std::cout << "log config error: appender type is null, " << a
                    << std::endl;
          continue;
        }
        std::string type = a["type"].as<std::string>();
        LogAppenderDefine lad;
        if (type == "FileLogAppender") {
          lad.type = 1;
          if (!a["file"].IsDefined()) {
            std::cout << "log config error: fileappender file is null, " << a
                      << std::endl;
            continue;
          }
          lad.file = a["file"].as<std::string>();
          if (a["formatter"].IsDefined()) {
            lad.formatter = a["formatter"].as<std::string>();
          }
        } else if (type == "StdoutLogAppender") {
          lad.type = 2;
          if (a["formatter"].IsDefined()) {
            lad.formatter = a["formatter"].as<std::string>();
          }
        } else {
          std::cout << "log config error: appender type is invalid, " << a
                    << std::endl;
          continue;
        }

        ld.appenders.push_back(lad);
      }
    }
    return ld;
  }
};

template <>
class LexicalCast<LogDefine, std::string> {
 public:
  std::string operator()(const LogDefine& i) {
    YAML::Node n;
    n["name"] = i.name;
    if (i.level != LogLevel::UNKNOW) {
      n["level"] = LogLevel::ToString(i.level);
    }
    if (!i.formatter.empty()) {
      n["formatter"] = i.formatter;
    }

    for (auto& a : i.appenders) {
      YAML::Node na;
      if (a.type == 1) {
        na["type"] = "FileLogAppender";
        na["file"] = a.file;
      } else if (a.type == 2) {
        na["type"] = "StdoutLogAppender";
      }
      if (a.level != LogLevel::UNKNOW) {
        na["level"] = LogLevel::ToString(a.level);
      }

      if (!a.formatter.empty()) {
        na["formatter"] = a.formatter;
      }

      n["appenders"].push_back(na);
    }
    std::stringstream ss;
    ss << n;
    return ss.str();
  }
};

sylar::ConfigVar<std::set<LogDefine> >::ptr g_log_defines =
    sylar::Config::Lookup("logs", std::set<LogDefine>(), "logs config");

struct LogIniter {
  LogIniter() {
    g_log_defines->addListener([](const std::set<LogDefine>& old_value,
                                  const std::set<LogDefine>& new_value) {
      SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "on_logger_conf_changed";
      for (auto& i : new_value) {
        auto it = old_value.find(i);
        sylar::Logger::ptr logger;
        if (it == old_value.end()) {
          //新增logger
          logger = SYLAR_LOG_NAME(i.name);
        } else {
          if (!(i == *it)) {
            //修改的logger
            logger = SYLAR_LOG_NAME(i.name);
          } else {
            continue;
          }
        }
        logger->setLevel(i.level);
        // std::cout << "** " << i.name << " level=" << i.level
        //<< "  " << logger << std::endl;
        if (!i.formatter.empty()) {
          logger->setFormatter(i.formatter);
        }

        logger->clearAppenders();
        for (auto& a : i.appenders) {
          sylar::LogAppender::ptr ap;
          if (a.type == 1) {
            ap.reset(new FileLogAppender(a.file));
          } else if (a.type == 2) {
            if (!sylar::EnvMgr::GetInstance()->has("d")) {
              ap.reset(new StdoutLogAppender);
            } else {
              continue;
            }
          }
          ap->setLevel(a.level);
          if (!a.formatter.empty()) {
            LogFormatter::ptr fmt(new LogFormatter(a.formatter));
            if (!fmt->isError()) {
              ap->setFormatter(fmt);
            } else {
              std::cout << "log.name=" << i.name << " appender type=" << a.type
                        << " formatter=" << a.formatter << " is invalid"
                        << std::endl;
            }
          }
          logger->addAppender(ap);
        }
      }

      for (auto& i : old_value) {
        auto it = new_value.find(i);
        if (it == new_value.end()) {
          //删除logger
          auto logger = SYLAR_LOG_NAME(i.name);
          logger->setLevel((LogLevel::Level)0);
          logger->clearAppenders();
        }
      }
    });
  }
};

static LogIniter __log_init;

std::string LoggerManager::toYamlString() {
  MutexType::Lock lock(m_mutex);
  YAML::Node node;
  for (auto& i : m_loggers) {
    node.push_back(YAML::Load(i.second->toYamlString()));
  }
  std::stringstream ss;
  ss << node;
  return ss.str();
}

void LoggerManager::init() {}

}  // namespace sylar
