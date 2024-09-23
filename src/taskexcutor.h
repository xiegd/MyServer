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

template<typename R, typename... ArgTypes>
class TaskCancelableImpl;
 
template<typename R, typename... ArgTypes>
// 类的偏特化, 表示返回值为R, 参数列表为ArgTypes...的函数
// 即适用于所有函数类型的偏特化
class TaskCancelableImpl<R(ArgTypes...)> : public TaskCacelable {
public:
    using Ptr = std::shared_ptr<TaskCancelableImpl>;
    using func_type = std::function<R(ArgTypes...)>;

    ~TaskCancelableImpl() = default;

    template<typename FUNC> 
    
protected:
    std::weak_ptr<func_type> weakTask_;
    std::shared_ptr<func_type> strongTask_;


};

#endif