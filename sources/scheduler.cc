#include "scheduler.h"

#include "hook.h"
#include "log.h"
#include "macro.h"

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

static thread_local Scheduler* t_scheduler = nullptr;
static thread_local Fiber* t_scheduler_fiber = nullptr;

Scheduler::Scheduler(size_t threads, bool use_caller, const std::string& name)
    : m_name(name) {
  SYLAR_ASSERT(threads > 0);
  // 如果使用caller线程（调度器所在线程）
  if (use_caller) {
    sylar::Fiber::GetThis();
    --threads;

    SYLAR_ASSERT(GetThis() == nullptr);
    t_scheduler = this;
    // 修改shared_ptr管理的指针
    m_rootFiber.reset(new Fiber(std::bind(&Scheduler::run, this), 0, true));
    sylar::Thread::SetName(m_name);

    t_scheduler_fiber = m_rootFiber.get();
    m_rootThread = sylar::GetThreadId();
    m_threadIds.push_back(m_rootThread);
  } else {
    m_rootThread = -1;
  }
  m_threadCount = threads;
}

Scheduler::~Scheduler() {
  SYLAR_ASSERT(m_stopping);
  if (GetThis() == this) {
    t_scheduler = nullptr;
  }
}

Scheduler* Scheduler::GetThis() { return t_scheduler; }

Fiber* Scheduler::GetMainFiber() { return t_scheduler_fiber; }

void Scheduler::start() {
  MutexType::Lock lock(m_mutex);
  if (!m_stopping) {
    return;
  }
  m_stopping = false;
  SYLAR_ASSERT(m_threads.empty());

  m_threads.resize(m_threadCount);
  for (size_t i = 0; i < m_threadCount; ++i) {
    m_threads[i].reset(new Thread(std::bind(&Scheduler::run, this),
                                  m_name + "_" + std::to_string(i)));
    m_threadIds.push_back(m_threads[i]->getId());
  }
  lock.unlock();

  // if(m_rootFiber) {
  //     //m_rootFiber->swapIn();
  //     m_rootFiber->call();
  //     SYLAR_LOG_INFO(g_logger) << "call out " << m_rootFiber->getState();
  // }
}

void Scheduler::stop() {
  m_autoStop = true;
  if (m_rootFiber && m_threadCount == 0 &&
      (m_rootFiber->getState() == Fiber::TERM ||
       m_rootFiber->getState() == Fiber::INIT)) {
    SYLAR_LOG_INFO(g_logger) << this << " stopped";
    m_stopping = true;

    if (stopping()) {
      return;
    }
  }

  // bool exit_on_this_fiber = false;
  if (m_rootThread != -1) {
    SYLAR_ASSERT(GetThis() == this);
  } else {
    SYLAR_ASSERT(GetThis() != this);
  }

  m_stopping = true;
  for (size_t i = 0; i < m_threadCount; ++i) {
    tickle();
  }

  if (m_rootFiber) {
    tickle();
  }

  if (m_rootFiber) {
    // while(!stopping()) {
    //     if(m_rootFiber->getState() == Fiber::TERM
    //             || m_rootFiber->getState() == Fiber::EXCEPT) {
    //         m_rootFiber.reset(new Fiber(std::bind(&Scheduler::run, this),
    //         0, true)); SYLAR_LOG_INFO(g_logger) << " root fiber is term,
    //         reset"; t_fiber = m_rootFiber.get();
    //     }
    //     m_rootFiber->call();
    // }
    if (!stopping()) {
      m_rootFiber->call();
    }
  }

  std::vector<Thread::ptr> thrs;
  {
    MutexType::Lock lock(m_mutex);
    thrs.swap(m_threads);
  }

  for (auto& i : thrs) {
    i->join();
  }
  // if(exit_on_this_fiber) {
  // }
}

void Scheduler::setThis() { t_scheduler = this; }

// 协程调度函数，从调度器的任务队列中取任务执行，取的任务即为子协程
void Scheduler::run() {
  SYLAR_LOG_DEBUG(g_logger) << m_name << " run";
  set_hook_enable(true);
  setThis();
  if (sylar::GetThreadId() != m_rootThread) {
    t_scheduler_fiber = Fiber::GetThis().get();  // 获取协程指针
  }
  // 构造执行idle函数的子协程, idle协程在无任务调度时执行
  Fiber::ptr idle_fiber(new Fiber(std::bind(&Scheduler::idle, this)));
  Fiber::ptr cb_fiber;

  FiberAndThread ft;
  // 进行协程调度和执行
  while (true) {
    ft.reset();
    bool tickle_me = false;  // 是否通知协程调度器，有需要调度的协程时进行通知
    bool is_active = false;
    {
      MutexType::Lock lock(m_mutex);
      auto it = m_fibers.begin();
      // 遍历协程队列, 选择下一个执行的协程
      while (it != m_fibers.end()) {
        // 寻找属于当前线程的协程
        if (it->thread != -1 && it->thread != sylar::GetThreadId()) {
          ++it;
          tickle_me = true;
          continue;
        }

        SYLAR_ASSERT(it->fiber || it->cb);  // 断言确保fiber或cb不为空
        // 如果找到的这个协程正在执行，则继续找
        if (it->fiber && it->fiber->getState() == Fiber::EXEC) {
          ++it;
          continue;
        }
        // 找到协程后
        ft = *it;
        m_fibers.erase(it++);  // 将选中的协程从协程队列删除
        ++m_activeThreadCount;
        is_active = true;  // 设置
        break;
      }
      // 如果迭代器没有到达尾部或者tickle_me已经被设置为了true, 则为true
      tickle_me |=
          it !=
          m_fibers
              .end();  // 将tickle_me和后面表达式进行`&`后的结果赋给tickle_me
    }

    if (tickle_me) {
      tickle();  // 通知协程调度器， 就打印了一条日志
    }
    // 如果当前协程不处于结束/异常状态（也不处于执行状态, 前面循环中判断了）
    if (ft.fiber && (ft.fiber->getState() != Fiber::TERM &&
                     ft.fiber->getState() != Fiber::EXCEPT)) {
      ft.fiber->swapIn();  // 将当前协程切换到运行状态
      --m_activeThreadCount;  // swapIn返回后，要么协程执行完，要么被yidld了
      // swapIn后不是进入EXEC状态？判断READY状态干什么？
      if (ft.fiber->getState() == Fiber::READY) {
        schedule(ft.fiber);  // 调度协程，添加到协程队列
      } else if (ft.fiber->getState() != Fiber::TERM &&
                 ft.fiber->getState() != Fiber::EXCEPT) {
        ft.fiber->m_state = Fiber::HOLD;
      }
      ft.reset();
    } else if (ft.cb) {
      if (cb_fiber) {
        cb_fiber->reset(ft.cb);
      } else {
        cb_fiber.reset(new Fiber(ft.cb));
      }
      ft.reset();
      cb_fiber->swapIn();
      --m_activeThreadCount;
      if (cb_fiber->getState() == Fiber::READY) {
        schedule(cb_fiber);
        cb_fiber.reset();
      } else if (cb_fiber->getState() == Fiber::EXCEPT ||
                 cb_fiber->getState() == Fiber::TERM) {
        cb_fiber->reset(nullptr);
      } else {  // if(cb_fiber->getState() != Fiber::TERM) {
        cb_fiber->m_state = Fiber::HOLD;
        cb_fiber.reset();
      }
    } else {
      // 任务队列恐龙，调度idle协程
      if (is_active) {
        --m_activeThreadCount;
        continue;
      }
      // 如果idle协程结束，则一定是调度器停止了
      if (idle_fiber->getState() == Fiber::TERM) {
        SYLAR_LOG_INFO(g_logger) << "idle fiber term";
        break;
      }

      ++m_idleThreadCount;
      idle_fiber->swapIn();
      --m_idleThreadCount;
      if (idle_fiber->getState() != Fiber::TERM &&
          idle_fiber->getState() != Fiber::EXCEPT) {
        idle_fiber->m_state = Fiber::HOLD;
      }
    }
  }
}

void Scheduler::tickle() { SYLAR_LOG_INFO(g_logger) << "tickle"; }

bool Scheduler::stopping() {
  MutexType::Lock lock(m_mutex);
  return m_autoStop && m_stopping && m_fibers.empty() &&
         m_activeThreadCount == 0;
}

void Scheduler::idle() {
  SYLAR_LOG_INFO(g_logger) << "idle";
  while (!stopping()) {
    sylar::Fiber::YieldToHold();
  }
}

void Scheduler::switchTo(int thread) {
  SYLAR_ASSERT(Scheduler::GetThis() != nullptr);
  if (Scheduler::GetThis() == this) {
    if (thread == -1 || thread == sylar::GetThreadId()) {
      return;
    }
  }
  schedule(Fiber::GetThis(), thread);
  Fiber::YieldToHold();
}

std::ostream& Scheduler::dump(std::ostream& os) {
  os << "[Scheduler name=" << m_name << " size=" << m_threadCount
     << " active_count=" << m_activeThreadCount
     << " idle_count=" << m_idleThreadCount << " stopping=" << m_stopping
     << " ]" << std::endl
     << "    ";
  for (size_t i = 0; i < m_threadIds.size(); ++i) {
    if (i) {
      os << ", ";
    }
    os << m_threadIds[i];
  }
  return os;
}

SchedulerSwitcher::SchedulerSwitcher(Scheduler* target) {
  m_caller = Scheduler::GetThis();
  if (target) {
    target->switchTo();
  }
}

SchedulerSwitcher::~SchedulerSwitcher() {
  if (m_caller) {
    m_caller->switchTo();
  }
}

}  // namespace sylar
