/**
 * 协程调度器封装
 */
#ifndef __SYLAR_SCHEDULER_H__
#define __SYLAR_SCHEDULER_H__

#include <iostream>
#include <list>
#include <memory>
#include <vector>

#include "fiber.h"
#include "thread.h"

namespace sylar {

// 协程调度器, 封装的是N-M的协程调度器,
// 内部有一个线程池,支持协程在线程池里面切换
class Scheduler {
 public:
  typedef std::shared_ptr<Scheduler> ptr;
  typedef Mutex MutexType;

  // 构造函数, threads 线程数量, use_caller 是否使用当前调用线程, name
  // 协程调度器名称
  Scheduler(size_t threads = 1, bool use_caller = true,
            const std::string& name = "");

  virtual ~Scheduler();                                  // 析构函数
  const std::string& getName() const { return m_name; }  // 返回协程调度器名称
  static Scheduler* GetThis();   // 返回当前协程调度器
  static Fiber* GetMainFiber();  // 返回当前协程调度器的调度协程
  void start();                  // 启动协程调度器
  void stop();                   // 停止协程调度器

  // 调度协程，
  template <class FiberOrCb>
  // fc, 协程或函数；thread，协程执行的线程id, -1标识任意线程
  void schedule(FiberOrCb fc, int thread = -1) {
    bool need_tickle = false;
    {
      MutexType::Lock lock(m_mutex);
      need_tickle = scheduleNoLock(fc, thread);
    }

    if (need_tickle) {
      tickle();
    }
  }

  // 批量调度协程
  template <class InputIterator>
  // begin, 协程数组的开始；end，协程数组的结束
  void schedule(InputIterator begin, InputIterator end) {
    bool need_tickle = false;
    {
      MutexType::Lock lock(m_mutex);
      while (begin != end) {
        need_tickle = scheduleNoLock(&*begin, -1) || need_tickle;
        ++begin;
      }
    }
    if (need_tickle) {
      tickle();
    }
  }

  void switchTo(int thread = -1);
  std::ostream& dump(std::ostream& os);

 protected:
  virtual void tickle();    // 通知协程调度器有任务了
  void run();               // 协程调度函数
  virtual bool stopping();  // 返回是否可以停止
  virtual void idle();      // 协程无任务可调度时执行idle协程
  void setThis();           // 设置当前的协程调度器
  bool hasIdleThreads() { return m_idleThreadCount > 0; }  // 是否有空闲线程
 private:
  // 协程调度启动(无锁)
  template <class FiberOrCb>
  bool scheduleNoLock(FiberOrCb fc, int thread) {
    bool need_tickle = m_fibers.empty();
    FiberAndThread ft(fc, thread);
    if (ft.fiber || ft.cb) {
      m_fibers.push_back(ft);
    }
    return need_tickle;
  }

 private:
  // 协程/函数/线程组
  struct FiberAndThread {
    Fiber::ptr fiber;          /// 协程
    std::function<void()> cb;  /// 协程执行函数
    int thread;                /// 线程id

    // 构造函数；f，协程；thr，线程id
    FiberAndThread(Fiber::ptr f, int thr) : fiber(f), thread(thr) {}

    // 构造函数，f，协程指针；thr，线程id；*f，nullptr
    FiberAndThread(Fiber::ptr* f, int thr) : thread(thr) { fiber.swap(*f); }

    // 构造函数，f，协程执行函数；thr，线程id
    FiberAndThread(std::function<void()> f, int thr) : cb(f), thread(thr) {}

    // 构造函数，f，协程执行函数指针；thr，线程id；*f = nullptr
    FiberAndThread(std::function<void()>* f, int thr) : thread(thr) {
      cb.swap(*f);
    }

    // 无参构造函数
    FiberAndThread() : thread(-1) {}

    // 重置数据
    void reset() {
      fiber = nullptr;
      cb = nullptr;
      thread = -1;
    }
  };

 private:
  MutexType m_mutex;                   /// Mutex
  std::vector<Thread::ptr> m_threads;  /// 线程池
  std::list<FiberAndThread> m_fibers;  /// 待执行的协程队列
  Fiber::ptr m_rootFiber;  /// use_caller为true时有效, 调度协程
  std::string m_name;      /// 协程调度器名称
 protected:
  std::vector<int> m_threadIds;                   /// 协程下的线程id数组
  size_t m_threadCount = 0;                       /// 线程数量
  std::atomic<size_t> m_activeThreadCount = {0};  /// 工作线程数量
  std::atomic<size_t> m_idleThreadCount = {0};    /// 空闲线程数量
  bool m_stopping = true;                         /// 是否正在停止
  bool m_autoStop = false;                        /// 是否自动停止
  int m_rootThread = 0;                           /// 主线程id(use_caller)
};

class SchedulerSwitcher : public Noncopyable {
 public:
  SchedulerSwitcher(Scheduler* target = nullptr);
  ~SchedulerSwitcher();

 private:
  Scheduler* m_caller;
};

}  // namespace sylar

#endif
