/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef Pipe_h
#define Pipe_h

#include <functional>

#include "EventPoller.h"
#include "PipeWrap.h"

namespace toolkit {

/**
 * @class Pipe
 * @brief 封装了管道通信的类
 *
 * 这个类提供了一个简单的接口来创建和使用管道，
 * 主要用于进程间通信或线程间通信。
 */
class Pipe {
public:
    /**
     * @typedef onRead
     * @brief 定义了读取管道数据时的回调函数类型
     * @param size 读取的数据大小
     * @param buf 读取的数据缓冲区
     */
    using onRead = std::function<void(int size, const char *buf)>;

    /**
     * @brief 构造函数
     * @param cb 读取数据时的回调函数
     * @param poller 事件轮询器，用于异步处理
     */
    Pipe(const onRead &cb = nullptr, const EventPoller::Ptr &poller = nullptr);

    /**
     * @brief 析构函数
     */
    ~Pipe();

    /**
     * @brief 向管道发送数据
     * @param send 要发送的数据
     * @param size 数据的大小，如果为0，则发送整个字符串
     */
    void send(const char *send, int size = 0);

private:
    std::shared_ptr<PipeWrap> _pipe;  ///< 底层管道包装对象
    EventPoller::Ptr _poller;         ///< 事件轮询器
};

}  // namespace toolkit
#endif /* Pipe_h */
