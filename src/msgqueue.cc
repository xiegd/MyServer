#include "msgqueue.h"
// #include <pthread.h>
#include <errno.h>
#include <stdlib.h>

#include <condition_variable>
#include <mutex>

template <typename Msg>
MsgQueue::MsgQueue() {
  Msg_max_ = 100;
  nonblock_ = false;
  msgs_ = new std::deque<Msg*>;
}

template <typename Msg>
MsgQueue::MsgQueue(size_t maxlen) {
  Msg_max_ = maxlen;
  nonblock_ = false;
  msgs_ = new std::deque<Msg*>;
}

template <typename Msg>
MsgQueue::~MsgQueue() {
  delete msgs_;
}

// 从消息队列队尾获取消息
template <typename Msg>
Msg* MsgQueue::getMsg() {
  Msg* message = nullptr;
  std::lock_guard<std::mutex> lock(get_mutex_);
  if (Msg_len_ > 0) {
    message = msgs_.back();
    msgs_.pop_back();
    Msg_len_--;
  }

  return message;
}

// 向消息队列队尾添加消息
template <typename Msg>
void MsgQueue::putMsg(Msg* message) {
  std::unique_lock<std::mutex> lock(put_mutex_);  // 作用域结束自动释放
  while (Msg_len_ > Msg_max_ - 1 && !nonblock_) {
    put_cond_.wait(lock);
  }
  msgs_.emplace_back(message);
  Msg_len_++;
  get_cond_.notify_one();  // 唤醒一个等待读消息的线程
}

template <typename Msg>
// 向消息队列队头添加消息
void MsgQueue::putMsgToHead(Msg* message) {
  std::unique_lock<std::mutex> lock(put_mutex_);
  while (Msg_len_ > Msg_max_ - 1 && !nonblock_) {
    put_cond_.wait(lock);
  }
  msgs_.emplace_front(message);
  Msg_len_++;
  get_cond_.notify_one();
}

// 设置为非阻塞模式，条件不满足会立即返回，而不是阻塞；
void MsgQueue::setNonblock() { nonblock_ = true; }

void MsgQueue::setBlock() {
  nonblock_ = false;
  put_mutex_.lock();
  get_cond_.notify_one();
  put_cond_.notify_all();
  put_mutex_.unlock();
}
