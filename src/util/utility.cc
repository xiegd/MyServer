#include "utility.h"

#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <pthread.h>
#include "logger.h"
#include "uv_errno.h"

namespace xkernel {
std::string& StringUtil::strToLower(std::string& str) {
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
    return str;
}

std::string StringUtil::strToLower(std::string&& str) {
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
    return std::move(str);
}

std::string& StringUtil::strToUpper(std::string& str) {
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
    return str;
}

std::string StringUtil::strToUpper(std::string&& str) {
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
    return std::move(str);
}

std::vector<std::string> StringUtil::split(const std::string& s, const char* delim) {
    std::vector<std::string> ret;
    size_t last = 0;
    auto index = s.find(delim, last);
    while (index != std::string::npos) {
        if (index - last > 0) {
            ret.push_back(s.substr(last, index - last));
        }
        last = index + strlen(delim);
        index = s.find(delim, last);
    }
    if (!s.size() || s.size() - last > 0) {
        ret.push_back(s.substr(last));
    }
    return ret;
}

std::string StringUtil::limitString(const char* name, size_t max_size) {
    std::string str = name;
    if (str.size() + 1 > max_size) {
        auto erased = str.size() + 1 - max_size + 3;  // 多移除3bytes替换为...
        str.replace(5, erased, "...");
    }
    return str;
}

bool StringUtil::startWith(const std::string& str, const std::string& substr) {
    return str.find(substr) == 0;
}
bool StringUtil::endWith(const std::string& str, const std::string& substr) {
    auto pos = str.rfind(substr);
    return pos != std::string::npos && pos == str.size() - substr.size();
}

semaphore::semaphore(size_t initial) : count_(initial) {}

semaphore::~semaphore() {}

// 增加信号量，可能唤醒等待的线程
void semaphore::post(size_t n) {
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    count_ += n;
    if (n == 1) {
        condition_.notify_one();
    }
    else {
        condition_.notify_all();
    }
}

// 等待并减少信号量
void semaphore::wait() {
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    while (count_ == 0) {
        condition_.wait(lock);
    }
    --count_;
}

void ThreadUtil::setThreadName(const char* name) {
    assert(name);  // 断言name不为空
    pthread_setname_np(pthread_self(), StringUtil::limitString(name, 16).data());
}

std::string ThreadUtil::getThreadName() {
    std::string ret;
    ret.resize(32);
    auto tid = pthread_self();
    pthread_getname_np(tid, (char*)ret.data(), ret.size());
    if (ret[0]) {
        ret.resize(strlen(ret.data()));
        return ret;
    }
    return std::to_string((uint64_t)tid);  // 如果没有设置name，则返回tid
}

// 设置线程的CPU亲和性
bool ThreadUtil::setThreadAffinity(int i) {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    if (i >= 0) {
        CPU_SET(i, &mask);  // 向mask中添加i号cpu核心
    }
    else {
        for (auto j = 0u; j < std::thread::hardware_concurrency(); ++j) {
            CPU_SET(j, &mask);
        }
    }
    if (!pthread_setaffinity_np(pthread_self(), sizeof(mask), &mask)) {
        return true;
    }
    WarnL << "pthread_setaffinity_np failed: " << get_uv_errmsg();
    return false;
}
} // namespace xkernel
