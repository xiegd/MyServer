#include "thread.h"
#include "log.h"
#include "util.h"

namespace sylar {

// 存储当前线程指针和线程名称
static thread_local Thread* t_thread = nullptr;
static thread_local std::string t_thread_name = "UNKNOW";

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

// 返回线程指针
Thread* Thread::GetThis() {
    return t_thread;
}

// 返回线程名称
const std::string& Thread::GetName() {
    return t_thread_name;
}

// 设置线程名称
void Thread::SetName(const std::string& name) {
    if(name.empty()) {
        return;
    }
    if(t_thread) {
        t_thread->m_name = name;
    }
    t_thread_name = name;
}

// 线程类的构造函数，
Thread::Thread(std::function<void()> cb, const std::string& name)
    :m_cb(cb)
    ,m_name(name) {
    if(name.empty()) {
        m_name = "UNKNOW";
    }
    int rt = pthread_create(&m_thread, nullptr, &Thread::run, this);  // 创建线程成功返回0；
    if(rt) {
        SYLAR_LOG_ERROR(g_logger) << "pthread_create thread fail, rt=" << rt
            << " name=" << name;
        throw std::logic_error("pthread_create error");
    }
    m_semaphore.wait();
}

Thread::~Thread() {
    if(m_thread) {
        pthread_detach(m_thread);  // 将线程标记为分离状态，在结束时会自动释放资源
    }
}

// 等待线程结束并回收资源
void Thread::join() {
    if(m_thread) {
        int rt = pthread_join(m_thread, nullptr);
        if(rt) {
            SYLAR_LOG_ERROR(g_logger) << "pthread_join thread fail, rt=" << rt
                << " name=" << m_name;
            throw std::logic_error("pthread_join error");
        }
        m_thread = 0;
    }
}

void* Thread::run(void* arg) {
    Thread* thread = (Thread*)arg;
    t_thread = thread;
    t_thread_name = thread->m_name;
    thread->m_id = sylar::GetThreadId();
    // 线程名称有长度限制，包含末尾null，共16个字符，
    pthread_setname_np(pthread_self(), thread->m_name.substr(0, 15).c_str());  // 将m_name的前15个字符作为线程名称，pthread_self()返回调用线程的线程ID

    std::function<void()> cb;
    cb.swap(thread->m_cb);  // 将传入Thread对象的回调函数和本地的cb对象交换，

    thread->m_semaphore.notify();  // 通知可能在等待线程完成的线程，表示当前线程已经就绪

    cb();  // 执行回调
    return 0;
}

}
