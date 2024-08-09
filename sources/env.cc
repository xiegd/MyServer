#include "env.h"
#include "sylar/log.h"
#include <string.h>
#include <iostream>
#include <iomanip>
#include <unistd.h>
#include <stdlib.h>
#include "config.h"

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

// 将main函数的参数原样传入，从中提取命令行选项和值，以及使用argv[0]获取命令行程序名
bool Env::init(int argc, char** argv) {
    char link[1024] = {0};
    char path[1024] = {0};
    sprintf(link, "/proc/%d/exe", getpid());  // 获取指向当前进程可执行文件的符号链接
    readlink(link, path, sizeof(path));  // 根据符号链接，获取当前可执行文件的绝对路径
    // /path/xxx/exe
    m_exe = path;

    auto pos = m_exe.find_last_of("/");
    m_cwd = m_exe.substr(0, pos) + "/";

    m_program = argv[0];  // 获取命令行输入的程序路径
    // -config /path/to/config -file xxxx -d
    const char* now_key = nullptr;
    for(int i = 1; i < argc; ++i) {
        if(argv[i][0] == '-') {
            if(strlen(argv[i]) > 1) {
                if(now_key) {
                    add(now_key, "");
                }
                now_key = argv[i] + 1;
            } else {
                SYLAR_LOG_ERROR(g_logger) << "invalid arg idx=" << i
                    << " val=" << argv[i];
                return false;
            }
        } else {
            if(now_key) {
                add(now_key, argv[i]);
                now_key = nullptr;
            } else {
                SYLAR_LOG_ERROR(g_logger) << "invalid arg idx=" << i
                    << " val=" << argv[i];
                return false;
            }
        }
    }
    if(now_key) {
        add(now_key, "");
    }
    return true;
}

// 添加一个自定义环境变量，如果已经有了，直接修改？😂
void Env::add(const std::string& key, const std::string& val) {
    RWMutexType::WriteLock lock(m_mutex);
    m_args[key] = val;
}

// 查找是否有自定义的环境变量key
bool Env::has(const std::string& key) {
    RWMutexType::ReadLock lock(m_mutex);
    auto it = m_args.find(key);
    return it != m_args.end();
}

// 删除一个自定义环境变量
void Env::del(const std::string& key) {
    RWMutexType::WriteLock lock(m_mutex);
    m_args.erase(key);
}

// 获取一个自定义环境变量的值
std::string Env::get(const std::string& key, const std::string& default_value) {
    RWMutexType::ReadLock lock(m_mutex);
    auto it = m_args.find(key);
    return it != m_args.end() ? it->second : default_value;
}

// 添加一个help
void Env::addHelp(const std::string& key, const std::string& desc) {
    removeHelp(key);
    RWMutexType::WriteLock lock(m_mutex);
    m_helps.push_back(std::make_pair(key, desc));
}

// 删除一个help
void Env::removeHelp(const std::string& key) {
    RWMutexType::WriteLock lock(m_mutex);
    for(auto it = m_helps.begin();
            it != m_helps.end();) {
        if(it->first == key) {
            it = m_helps.erase(it);
        } else {
            ++it;
        }
    }
}

// 打印所有的help，名称和内容
void Env::printHelp() {
    RWMutexType::ReadLock lock(m_mutex);
    std::cout << "Usage: " << m_program << " [options]" << std::endl;
    for(auto& i : m_helps) {
        std::cout << std::setw(5) << "-" << i.first << " : " << i.second << std::endl;
    }
}

// 设置系统环境变量
bool Env::setEnv(const std::string& key, const std::string& val) {
    return !setenv(key.c_str(), val.c_str(), 1);
}

// 获取系统环境变量的值
std::string Env::getEnv(const std::string& key, const std::string& default_value) {
    const char* v = getenv(key.c_str());
    if(v == nullptr) {
        return default_value;
    }
    return v;
}

// 获取绝对路径, 传入一个相对于bin文件的路径，返回对应的绝对路径
std::string Env::getAbsolutePath(const std::string& path) const {
    if(path.empty()) {
        return "/";
    }
    if(path[0] == '/') {
        return path;
    }
    return m_cwd + path;
}

// 获取绝对工作路径
std::string Env::getAbsoluteWorkPath(const std::string& path) const {
    if(path.empty()) {
        return "/";
    }
    if(path[0] == '/') {
        return path;
    }
    static sylar::ConfigVar<std::string>::ptr g_server_work_path =
        sylar::Config::Lookup<std::string>("server.work_path");
    return g_server_work_path->getValue() + "/" + path;
}

// 获取配置文件夹路径
std::string Env::getConfigPath() {
    return getAbsolutePath(get("c", "conf"));
}

}
