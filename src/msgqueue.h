/*
* 消息队列, 从队列头部取消息，从队列尾部放消息, 对于重要消息可以调用putMsgToHead放到队头
* 非阻塞模式下，如果队列满了，则不放入消息
*/
#ifndef _MSGQUEUE_H_
#define _MSGQUEUE_H_

// #include <pthread.h>
#include <stdlib.h>

#include <condition_variable>  // 使用condition_variable代替pthread_cond_t
#include <deque>
#include <mutex>  // 使用mutex代替pthread_mutex_t

template <typename Msg>
class MsgQueue {
 public:
  MsgQueue() {
    Msg_max_ = 100;
    nonblock_ = false;
    msgs_ = new std::deque<Msg*>;
  }
  MsgQueue(size_t maxlen) {
    Msg_max_ = maxlen;
    nonblock_ = false;
    msgs_ = new std::deque<Msg*>;
  }

  ~MsgQueue() {
    delete msgs_;
  }

 public:
  // 从队列头部取消息
  Msg* getMsg() {
    Msg* message = nullptr;
    std::unique_lock<std::mutex> lock(get_mutex_);
    // 阻塞模式下，如果队列中没有消息，则等待
    while (Msg_len_ == 0 && !nonblock_) {
      get_cond_.wait(lock);
    }
    // 非阻塞模式返回nullptr
    if (Msg_len_ == 0) {
      return nullptr;
    }
    message = msgs_->front();
    msgs_->pop_front();
    Msg_len_--;
    // 如果之前队列时满的，则通知生产者，有空间了
    if (Msg_len_ == Msg_max_ - 1) {
      put_cond_.notify_one();
    }
    return message;
  }

  void putMsg(Msg* message) {
    std::unique_lock<std::mutex> lock(put_mutex_);  // 作用域结束自动释放
    while (Msg_len_ > Msg_max_ && !nonblock_) {
      put_cond_.wait(lock);
    }
    // 非阻塞模式下，如果队列满了，则不放入消息
    if (Msg_len_ == Msg_max_ && nonblock_) {
      return;
    }
    msgs_->emplace_back(message);
    Msg_len_++;
    get_cond_.notify_one();  // 唤醒一个等待读消息的线程
  }

  // 向队列头部插入消息
  void putMsgToHead(Msg* message) {
    std::unique_lock<std::mutex> lock(put_mutex_);
    while (Msg_len_ > Msg_max_ && !nonblock_) {
      put_cond_.wait(lock);
    }
    msgs_->emplace_front(message);
    Msg_len_++;
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
  size_t size() { return Msg_len_; }

 private:
  size_t Msg_max_;
  size_t Msg_len_;
  bool nonblock_;  // 默认阻塞
  std::deque<Msg*> *msgs_;
  std::mutex get_mutex_;
  std::mutex put_mutex_;
  std::condition_variable get_cond_;
  std::condition_variable put_cond_;
};

#endif