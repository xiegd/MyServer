#include <iostream>
#include "log.h"

#define VERSION "1.0"

using namespace std;

class LogLevel {
public: 
    // 日志级别枚举
    enum class Level {
        UNKNOW = 0,
        DEBUG = 1,
        INFO = 2,
        WARN = 3,
        ERROR = 4,
        FATAL = 5
    };

        enum class Level2 {
        UNKNOW = 1,
        DEBUG = 2,
        INFO = 3,
        WARN = 4,
        ERROR = 5,
        FATAL = 6
    };
    int i ;

    static const char* ToString(LogLevel::Level level);     //将日志级别转化为文本输出
    static LogLevel::Level FromString(const std::string& str);    // 将文本转换为日志级别
};

int main() {
    cout << "aaa" << endl;
    cout << __cplusplus << endl;
    cout << "project version: " << VERSION << endl;
    LogLevel level;
}
