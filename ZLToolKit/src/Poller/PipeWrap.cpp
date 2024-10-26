/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "PipeWrap.h"

#include <stdexcept>

#include "Network/sockutil.h"
#include "Util/util.h"
#include "Util/uv_errno.h"

using namespace std;

/**
 * @brief 检查文件描述符的宏
 * 
 * 如果文件描述符无效（-1），则清理资源并抛出运行时错误
 */
#define checkFD(fd)                                                      \
    if (fd == -1) {                                                      \
        clearFD();                                                       \
        throw runtime_error(StrPrinter << "Create windows pipe failed: " \
                                       << get_uv_errmsg());              \
    }

/**
 * @brief 关闭文件描述符的宏
 * 
 * 如果文件描述符有效，则关闭它并将其设置为-1
 */
#define closeFD(fd) \
    if (fd != -1) { \
        close(fd);  \
        fd = -1;    \
    }

namespace toolkit {

/**
 * @brief PipeWrap类的构造函数
 * 
 * 创建一个新的管道
 */
PipeWrap::PipeWrap() { reOpen(); }

/**
 * @brief 重新打开管道
 * 
 * 关闭现有的管道（如果有），并创建一个新的管道
 */
void PipeWrap::reOpen() {
    clearFD();
#if defined(_WIN32)
    // Windows平台使用套接字对来模拟管道
    const char *localip = SockUtil::support_ipv6() ? "::1" : "127.0.0.1";
    // 创建监听socket
    auto listener_fd = SockUtil::listen(0, localip);  // 指定0号端口，则操作系统会自动分配一个空闲端口
    checkFD(listener_fd) SockUtil::setNoBlocked(listener_fd, false);
    auto localPort = SockUtil::get_local_port(listener_fd);  // 获取listener_fd的本地端口
    // 创建连接socket, 连接到listener_fd, 为了创建了一个从_pipe_fd[1]到listener_fd的连接
    _pipe_fd[1] = SockUtil::connect(localip, localPort, false);
    checkFD(_pipe_fd[1]) 
    // 接受连接， 为了创建了一个从listener_fd到_pipe_fd[0]的连接
    // 返回一个新的文件描述符用来和_pipe_fd[0]通信
    _pipe_fd[0] = (int)accept(listener_fd, nullptr, nullptr);
    checkFD(_pipe_fd[0]) SockUtil::setNoDelay(_pipe_fd[0]);
    SockUtil::setNoDelay(_pipe_fd[1]);
    close(listener_fd);
#else
    // 非Windows平台使用系统的pipe函数
    if (pipe(_pipe_fd) == -1) {
        throw runtime_error(StrPrinter << "Create posix pipe failed: "
                                       << get_uv_errmsg());
    }
#endif  // defined(_WIN32)
    // 设置读端为非阻塞模式
    SockUtil::setNoBlocked(_pipe_fd[0], true);
    // 设置写端为阻塞模式
    SockUtil::setNoBlocked(_pipe_fd[1], false);
    // 设置读端和写端为close-on-exec模式
    SockUtil::setCloExec(_pipe_fd[0]);
    SockUtil::setCloExec(_pipe_fd[1]);
}

/**
 * @brief 清理文件描述符
 * 
 * 关闭管道的两端（如果它们是打开的）
 */
void PipeWrap::clearFD() {
    closeFD(_pipe_fd[0]);
    closeFD(_pipe_fd[1]);
}

/**
 * @brief PipeWrap类的析构函数
 * 
 * 清理资源，关闭管道
 */
PipeWrap::~PipeWrap() { clearFD(); }

/**
 * @brief 向管道写入数据
 * @param buf 要写入的数据缓冲区
 * @param n 要写入的字节数
 * @return 实际写入的字节数，如果出错则返回-1
 */
int PipeWrap::write(const void *buf, int n) {
    int ret;
    do {
#if defined(_WIN32)
        ret = send(_pipe_fd[1], (char *)buf, n, 0);
#else
        ret = ::write(_pipe_fd[1], buf, n);
#endif  // defined(_WIN32)
    } while (-1 == ret && UV_EINTR == get_uv_error(true));
    return ret;
}

/**
 * @brief 从管道读取数据
 * @param buf 用于存储读取数据的缓冲区
 * @param n 要读取的最大字节数
 * @return 实际读取的字节数，如果出错则返回-1
 */
int PipeWrap::read(void *buf, int n) {
    int ret;
    do {
#if defined(_WIN32)
        ret = recv(_pipe_fd[0], (char *)buf, n, 0);
#else
        ret = ::read(_pipe_fd[0], buf, n);
#endif  // defined(_WIN32)
    } while (-1 == ret && UV_EINTR == get_uv_error(true));
    return ret;
}

} /* namespace toolkit*/
