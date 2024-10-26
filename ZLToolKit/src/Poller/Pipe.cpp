/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "Pipe.h"

#include <fcntl.h>

#include "Network/sockutil.h"

using namespace std;

namespace toolkit {

/**
 * @brief Pipe类的构造函数
 * @param cb 读取数据时的回调函数
 * @param poller 事件轮询器，如果为nullptr则使用EventPollerPool中的一个
 */
Pipe::Pipe(const onRead &cb, const EventPoller::Ptr &poller) {
    // 如果没有提供poller，则从EventPollerPool中获取一个
    _poller = poller;
    if (!_poller) {
        _poller = EventPollerPool::Instance().getPoller();
    }
    
    // 创建PipeWrap对象
    _pipe = std::make_shared<PipeWrap>();
    auto pipe = _pipe;
    
    // 为管道的读取端添加读事件
    _poller->addEvent(
        _pipe->readFD(), EventPoller::Event_Read, [cb, pipe](int event) {
#if defined(_WIN32)
            unsigned long nread = 1024;
#else
            int nread = 1024;
#endif  // defined(_WIN32)
            // 获取可读数据的大小
            ioctl(pipe->readFD(), FIONREAD, &nread);
#if defined(_WIN32)
            // Windows平台特定的读取逻辑
            std::shared_ptr<char> buf(new char[nread + 1],
                                      [](char *ptr) { delete[] ptr; });
            buf.get()[nread] = '\0';
            nread = pipe->read(buf.get(), nread + 1);
            if (cb) {
                cb(nread, buf.get());
            }
#else
            // 非Windows平台的读取逻辑
            char buf[nread + 1];
            buf[nread] = '\0';
            nread = pipe->read(buf, sizeof(buf));
            if (cb) {
                cb(nread, buf);
            }
#endif  // defined(_WIN32)
        });
}

/**
 * @brief Pipe类的析构函数
 * 
 * 移除管道读取端的事件监听
 */
Pipe::~Pipe() {
    if (_pipe) {
        auto pipe = _pipe;
        _poller->delEvent(pipe->readFD(), [pipe](bool success) {});
    }
}

/**
 * @brief 向管道发送数据
 * @param buf 要发送的数据缓冲区
 * @param size 要发送的数据大小，如果为0则发送整个字符串
 */
void Pipe::send(const char *buf, int size) { 
    _pipe->write(buf, size); 
}

}  // namespace toolkit
