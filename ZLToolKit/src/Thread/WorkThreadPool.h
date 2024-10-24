/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef UTIL_WORKTHREADPOOL_H_
#define UTIL_WORKTHREADPOOL_H_

#include <memory>

#include "Poller/EventPoller.h"

namespace toolkit {

/**
 * @class WorkThreadPool
 * @brief 工作线程池类,实现了单例模式和任务执行器获取接口
 * 
 * 该类管理一组EventPoller实例,用于处理异步任务
 */
class WorkThreadPool : public std::enable_shared_from_this<WorkThreadPool>,
                       public TaskExecutorGetterImp {
   public:
    /**
     * @typedef Ptr
     * @brief WorkThreadPool的智能指针类型
     */
    using Ptr = std::shared_ptr<WorkThreadPool>;

    /**
     * @brief 虚析构函数
     */
    ~WorkThreadPool() override = default;

    /**
     * @brief 获取WorkThreadPool单例实例
     * @return WorkThreadPool& 单例实例的引用
     */
    static WorkThreadPool &Instance();

    /**
     * @brief 设置EventPoller实例的数量
     * 
     * 此方法必须在WorkThreadPool单例创建之前调用才有效
     * 如果不调用此方法,默认创建thread::hardware_concurrency()个EventPoller实例
     * 
     * @param size EventPoller实例的数量,如果为0则使用thread::hardware_concurrency()
     */
    static void setPoolSize(size_t size = 0);

    /**
     * @brief 设置是否启用CPU亲和性
     * 
     * 控制内部创建的线程是否设置CPU亲和性,默认启用
     * 
     * @param enable 是否启用CPU亲和性
     */
    static void enableCpuAffinity(bool enable);

    /**
     * @brief 获取第一个EventPoller实例
     * @return EventPoller::Ptr 第一个EventPoller实例的智能指针
     */
    EventPoller::Ptr getFirstPoller();

    /**
     * @brief 获取负载较轻的EventPoller实例
     * 
     * 根据负载情况选择一个轻负载的实例
     * 如果当前线程已经绑定了一个EventPoller,则优先返回当前线程的实例
     * 返回当前线程的实例可以提高线程安全性
     * 
     * @return EventPoller::Ptr 选中的EventPoller实例的智能指针
     */
    EventPoller::Ptr getPoller();

   protected:
    /**
     * @brief 构造函数
     * 
     * 构造函数被声明为protected,以支持单例模式
     */
    WorkThreadPool();
};

} /* namespace toolkit */
#endif /* UTIL_WORKTHREADPOOL_H_ */
