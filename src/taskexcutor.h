#ifndef _TASKEXCUTOR_H_
#define _TASKEXCUTOR_H_

#include <functional>
#include <memory>
#include <mutex>
#include "utility.h"

class ThreadLoadCounter {

};

// 可取消任务的抽象基类
class TaskCacelable : public Noncopyable {
public:
    TaskCacelable() = default;
    virtual ~TaskCacelable() = default;

    virtual void cancel() = 0;
};

template<class R, class... ArgTypes>
class TaskCancelableImpl;


template<class R, class... ArgTypes>
class TaskCancelableImpl<R(ArgTypes...)> : public TaskCacelable {

}

#endif