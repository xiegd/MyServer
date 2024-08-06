/**
 * @file thread.h
 * @brief 线程相关的封装
 */
#ifndef __SYLAR_THREAD_H__
#define __SYLAR_THREAD_H__

#include "mutex.h"
#include <string>

namespace sylar {

// 线程类, Noncopyable一个基类，禁止了拷贝构造函数和赋值运算符
class Thread : Noncopyable {
public:
    typedef std::shared_ptr<Thread> ptr;  /// 线程智能指针类型

    Thread(std::function<void()> cb, const std::string& name);  // 构造函数, cb 线程执行函数, name 线程名称
    ~Thread();

    pid_t getId() const { return m_id;}  // 线程ID
    const std::string& getName() const { return m_name;}  // 线程名称
    void join();  // 等待线程执行完成
    static Thread* GetThis();  // 获取当前的线程指针
    static const std::string& GetName();  // 获取当前的线程名称
    static void SetName(const std::string& name);  // 设置当前线程名称
private:
    static void* run(void* arg);  // 线程执行函数
private:
    pid_t m_id = -1;  /// 线程id
    pthread_t m_thread = 0;  /// 线程结构
    std::function<void()> m_cb;  /// 回调函数
    std::string m_name;  /// 线程名称
    Semaphore m_semaphore;  /// 信号量
};

}

#endif
