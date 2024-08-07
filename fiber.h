/**
 * 协程封装
 */
#ifndef __SYLAR_FIBER_H__
#define __SYLAR_FIBER_H__

#include <memory>
#include <functional>
#include <ucontext.h>

namespace sylar {

class Scheduler;

// 协程类
class Fiber : public std::enable_shared_from_this<Fiber> {
friend class Scheduler;
public:
    typedef std::shared_ptr<Fiber> ptr;

    // 协程状态
    enum State {
        INIT,  /// 初始化状态
        READY,  /// 可执行状态
        HOLD,  /// 暂停状态
        EXEC,  /// 执行中状态
        TERM,  /// 结束状态
        EXCEPT  /// 异常状态
    };
private:
    Fiber();   // 无参数构造函数，每个线程第一个协程的构造

public:
    /**
     * @brief 构造函数
     * @param[in] cb 协程执行的函数
     * @param[in] stacksize 协程栈大小
     * @param[in] use_caller 是否在MainFiber上调度
     */
    Fiber(std::function<void()> cb, size_t stacksize = 0, bool use_caller = false);
    ~Fiber();

    void reset(std::function<void()> cb);  // 重置协程执行函数，并设置为初始化状态
    void swapIn();  //  将当前协程切换到运行状态
    void swapOut();  // 将当前协程切换到后台
    void call();  // 将当前线程切换到执行状态，执行的是当前线程的主协程
    void back();  // 将当前线程切换到后台，pre执行的为该协程，post返回到线程的主协程
    uint64_t getId() const { return m_id;}  // 返回协程id
    State getState() const { return m_state;}  // 返回协程状态

public: 
    // 静态成员函数，所有对象实例共享，没有this指针
    static void SetThis(Fiber* f);  // 设置当前线程的运行协程
    static Fiber::ptr GetThis();  // 返回当前所在的协程
    static void YieldToReady();  // 将当前协程切换到后台，并设置为READY状态
    static void YieldToHold();  // 将当前协程切换到后台，并设置为HOLD状态
    static uint64_t TotalFibers();  // 返回当前协程的总数量
    static void MainFunc();  // 协程执行函数，执行完成返回到线程主协程
    static void CallerMainFunc();  // 协程执行函数，执行完成返回到线程调度协程
    static uint64_t GetFiberId();  // 获取当前协程的id

private:
    uint64_t m_id = 0;  /// 协程id
    uint32_t m_stacksize = 0;  /// 协程运行栈大小
    State m_state = INIT;  /// 协程状态
    ucontext_t m_ctx;  /// 协程上下文
    void* m_stack = nullptr;  /// 协程运行栈指针
    std::function<void()> m_cb;  /// 协程运行函数
};

}

#endif
