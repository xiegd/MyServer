/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

/**
 * @file BufferSock.h
 * @brief 定义了与网络缓冲区和套接字操作相关的类
 *
 * 本文件包含了BufferSock、BufferList和SocketRecvBuffer类的定义，
 * 这些类用于处理网络数据的缓冲、发送和接收操作。
 */

#ifndef ZLTOOLKIT_BUFFERSOCK_H
#define ZLTOOLKIT_BUFFERSOCK_H

// 如果不是Windows平台
#if !defined(_WIN32)
// 定义了各种数值类型的最大最小值宏等
#include <limits.h>
// 定义了iovec结构体，用于描述一块内存区域
#include <sys/uio.h>
#endif
#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "Buffer.h"
#include "Util/List.h"
#include "Util/ResourcePool.h"
#include "Util/util.h"
#include "sockutil.h"

namespace toolkit {

#if !defined(IOV_MAX)
#define IOV_MAX 1024
#endif

/**
 * @class BufferSock
 * @brief 结合了缓冲区和套接字地址信息的类
 *
 * BufferSock类继承自Buffer，并额外包含了套接字地址信息。
 * 这对于需要同时处理数据内容和网络地址的场景（如UDP通信）非常有用。
 */
class BufferSock : public Buffer {
   public:
    using Ptr = std::shared_ptr<BufferSock>;

    /**
     * @brief 构造函数
     * @param ptr 指向Buffer对象的智能指针
     * @param addr 套接字地址结构指针，默认为nullptr
     * @param addr_len 地址结构的长度，默认为0
     */
    BufferSock(Buffer::Ptr ptr, struct sockaddr *addr = nullptr,
               int addr_len = 0);

    /**
     * @brief 虚析构函数
     */
    ~BufferSock() override = default;

    /**
     * @brief 获取数据指针
     * @return 指向数据的字符指针
     */
    char *data() const override;

    /**
     * @brief 获取数据大小
     * @return 数据的字节数
     */
    size_t size() const override;

    /**
     * @brief 获取套接字地址
     * @return 指向套接字地址结构的常量指针
     */
    const struct sockaddr *sockaddr() const;

    /**
     * @brief 获取套接字地址长度
     * @return 套接字地址结构的长度
     */
    socklen_t socklen() const;

   private:
    int _addr_len = 0;                  ///< 地址结构的长度
    struct sockaddr_storage _addr;      ///< 存储套接字地址的结构
    Buffer::Ptr _buffer;                ///< 指向实际数据缓冲区的智能指针
};

/**
 * @class BufferList
 * @brief 管理多个缓冲区的列表，用于批量发送数据
 *
 * BufferList类提供了一个接口，用于管理多个Buffer对象，并能够批量发送它们。
 * 这通常用于优化网络传输，减少系统调用的次数。
 */
class BufferList : public noncopyable {
   public:
    using Ptr = std::shared_ptr<BufferList>;
    using SendResult =
        std::function<void(const Buffer::Ptr &buffer, bool send_success)>;

    BufferList() = default;
    virtual ~BufferList() = default;

    /**
     * @brief 检查列表是否为空
     * @return 如果列表为空返回true，否则返回false
     */
    virtual bool empty() = 0;

    /**
     * @brief 获取列表中的缓冲区数量
     * @return 缓冲区的数量
     */
    virtual size_t count() = 0;

    /**
     * @brief 尝试发送列表中的所有数据
     * @param fd 文件描述符
     * @param flags 发送标志
     * @return 发送的字节数，或者错误码
     */
    virtual ssize_t send(int fd, int flags) = 0;

    /**
     * @brief 创建一个BufferList实例
     * @param list 包含Buffer::Ptr和bool标志的列表
     * @param cb 发送结果的回调函数
     * @param is_udp 是否为UDP模式
     * @return 指向创建的BufferList对象的智能指针
     */
    static Ptr create(List<std::pair<Buffer::Ptr, bool> > list, SendResult cb,
                      bool is_udp);

   private:
    // 对象个数统计
    ObjectStatistic<BufferList> _statistic;
};

/**
 * @class SocketRecvBuffer
 * @brief 用于接收套接字数据的缓冲区接口
 *
 * SocketRecvBuffer类定义了一个接口，用于从套接字接收数据并管理接收缓冲区。
 * 它支持UDP和TCP模式，并能够处理多个接收缓冲区。
 * 使用了工厂方法模式
 */
class SocketRecvBuffer {
   public:
    using Ptr = std::shared_ptr<SocketRecvBuffer>;

    virtual ~SocketRecvBuffer() = default;

    /**
     * @brief 从套接字接收数据
     * @param fd 文件描述符
     * @param count 接收的字节数（输出参数）
     * @return 接收操作的结果，成功返回接收的字节数，失败返回-1
     * ssize_t是size_t的有符号版本，最大值通常与size_t相同，但少一位，
     * 使用负值来表示错误状态
     */
    virtual ssize_t recvFromSocket(int fd, ssize_t &count) = 0;

    /**
     * @brief 获取指定索引的缓冲区
     * @param index 缓冲区的索引
     * @return 指向Buffer对象的引用
     */
    virtual Buffer::Ptr &getBuffer(size_t index) = 0;

    /**
     * @brief 获取指定索引的地址信息
     * @param index 地址信息的索引
     * @return 指向sockaddr_storage结构的引用
     */
    virtual struct sockaddr_storage &getAddress(size_t index) = 0;

    /**
     * @brief 创建一个SocketRecvBuffer实例
     * @param is_udp 是否为UDP模式
     * @return 指向创建的SocketRecvBuffer对象的智能指针
     */
    static Ptr create(bool is_udp);
};

}  // namespace toolkit
#endif  // ZLTOOLKIT_BUFFERSOCK_H