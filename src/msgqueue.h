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
  Msg_queue();
  Msg_queue(size_t maxlen);

  ~Msg_queue();

 public:
  Msg* getMsg();
  void putMsg(Msg* message);
  void putMsgToHead(Msg* messsage);

  void setNonblock();
  void setBlock();

 private:
  size_t Msg_max_;
  size_t Msg_len_;
  bool nonblock_;  // 默认阻塞
  std::deque<Msg*> msgs_;
  std::mutex get_mutex_;
  std::mutex put_mutex_;
  std::condition_variable get_cond_;
  std::condition_variable put_cond_;
};

#endif