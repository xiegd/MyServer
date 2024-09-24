/*
* 消息队列, 从队列头部取消息，从队列尾部放消息, 对于重要消息可以调用putMsgToHead放到队头
* 非阻塞模式下，如果队列满了，则不放入消息
*/
#ifndef _MSGQUEUE_H_
#define _MSGQUEUE_H_

#include <stdlib.h>
#include <condition_variable>  // 使用condition_variable代替pthread_cond_t
#include <deque>
#include <mutex>  // 使用mutex代替pthread_mutex_t
#include <optional>
#include <atomic>

template <typename Msg>
class MsgQueue {
 public:
  MsgQueue() {
    Msg_max_ = 100;
    Msg_len_.store(0);
    nonblock_ = false;
  }
  MsgQueue(size_t maxlen) {
    Msg_max_ = maxlen;
    Msg_len_.store(0);
    nonblock_ = false;
  }

  ~MsgQueue() {}

 public:
  // 从队列头部取消息
  bool getMsg(Msg& message) {
    std::unique_lock<std::mutex> lock(get_mutex_);
    // 非阻塞模式下
    if (Msg_len_.load() == 0 && nonblock_) {
      return false;  // 没有消息则返回默认构造的Msg
    }
    // 阻塞模式下，如果队列中没有消息，则等待
    while (Msg_len_.load() == 0 && !nonblock_) {
      get_cond_.wait(lock);
    }
    message = std::move(msgs_.front());
    msgs_.pop_front();
    Msg_len_.fetch_sub(1);
    // 如果之前队列时满的，则通知生产者，有空间了
    if (Msg_len_.load() == Msg_max_ - 1) {
      put_cond_.notify_one();
    }
    return true;
  }

  void putMsg(Msg message) {
    std::unique_lock<std::mutex> lock(put_mutex_);  // 作用域结束自动释放
    while (Msg_len_.load() > Msg_max_ && !nonblock_) {
      put_cond_.wait(lock);
    }
    // 非阻塞模式下，如果队列满了，则不放入消息
    if (Msg_len_.load() == Msg_max_ && nonblock_) {
      return;
    }
    msgs_.emplace_back(std::move(message));
    Msg_len_.fetch_add(1);
    get_cond_.notify_one();  // 唤醒一个等待读消息的线程
  }

  // 向队列头部插入消息
  void putMsgToHead(Msg message) {
    std::unique_lock<decltype(put_mutex_)> lock(get_mutex_);  // 头插只需要和get操作互斥
    while (Msg_len_.load() > Msg_max_ && !nonblock_) {
      put_cond_.wait(lock);
    }
    msgs_.emplace_front(std::move(message));
    Msg_len_.fetch_add(1);
    get_cond_.notify_one();
  }

  void setNonblock() { nonblock_ = true; }
  void setBlock() {
    nonblock_ = false;
    put_mutex_.lock();
    get_cond_.notify_one();
    put_cond_.notify_all();
    put_mutex_.unlock();
  }
  size_t size() const{
    return Msg_len_.load();
  }

 private:
  size_t Msg_max_;
  std::atomic<size_t> Msg_len_;
  bool nonblock_;
  std::deque<Msg> msgs_;
  std::mutex get_mutex_;
  std::mutex put_mutex_;
  std::mutex len_mutex_;  // 控制对Msg_len_互斥访问
  std::condition_variable get_cond_;
  std::condition_variable put_cond_;
};

#endif