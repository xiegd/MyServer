#ifndef __SYLAR_ENV_H__
#define __SYLAR_ENV_H__

#include "sylar/singleton.h"
#include "sylar/thread.h"
#include <map>
#include <vector>

namespace sylar {

// 包装为单例模式，保证程序的环境变量是全局唯一的
class Env {
public:
    typedef RWMutex RWMutexType;
    bool init(int argc, char** argv);  // 环境变量模块初始化
    // add/has/del/get用于操作程序自定义环境变量，key-value形式
    void add(const std::string& key, const std::string& val);
    bool has(const std::string& key);
    void del(const std::string& key);
    std::string get(const std::string& key, const std::string& default_value = "");
    // 操作帮助选项和描述信息
    void addHelp(const std::string& key, const std::string& desc);
    void removeHelp(const std::string& key);
    void printHelp();
    
    const std::string& getExe() const { return m_exe;}  // 获取程序名
    const std::string& getCwd() const { return m_cwd;}  // 获取程序路径

    // 为统一接口
    bool setEnv(const std::string& key, const std::string& val);  // 对应标准库的setenv
    std::string getEnv(const std::string& key, const std::string& default_value = "");  // 对应标准库的getenv

    std::string getAbsolutePath(const std::string& path) const;  // 获取绝对路径
    std::string getAbsoluteWorkPath(const std::string& path) const;
    std::string getConfigPath();  // 获取配置文件夹路径，由命令行-c选项传入
private:
    RWMutexType m_mutex;
    std::map<std::string, std::string> m_args;  // 程序自定义环境变量
    std::vector<std::pair<std::string, std::string> > m_helps;  // 帮助信息

    std::string m_program;  // 
    std::string m_exe;  // 程序的绝对路径
    std::string m_cwd;  // 程序路径，m_exe去除最后的文件名
};

typedef sylar::Singleton<Env> EnvMgr;

}

#endif
