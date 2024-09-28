/*
 * buffersock.h
 * 
 */

#ifndef _BUFFERSOCK_H_
#define _BUFFERSOCK_H_

#include <limits.h>
#include <sys/uio.h>
#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "buffer.h"
#include "utility.h"

// 封装socket信息和相应的buffer
class BuffferSock : public Buffer {
public:
    using Ptr = std::shared_ptr<BuffferSock>;

    BufferSock(Buffer::Ptr ptr, struct sockaddr *addr = nullptr, int addr_len = 0);
    ~BufferSock() override = default;

public:
    char* data() const override;
    size_t size() const override;
    const struct sockaddr* sockaddr() const;
    socklen_t socklen() const;

private:
    int addr_len_ = 0;
    struct sockaddr_storage addr_;
    Buffer::Ptr buffer_;
};

// 用于接收套接字数据的缓冲区接口
class SocketRecvBuffer {
public:
    using Ptr = std::shared_ptr<SocketRecvBuffer>;

    static Ptr create(bool is_udp);
    SocketRecvBuffer() = default;
    virtual ~SocketRecvBuffer() = default;

public:
    virtual ssize_t recvFromSocket(int fd, ssize_t& count) = 0;
    virtual Buffer::Ptr& getBuffer(size_t index) = 0;
    virtual struct sockaddr_storage& getAddress(size_t index) = 0;
};

// BufferList接口用于管理多个Buffer对象
class BufferList : public Noncopyable {
public:
    using Ptr = std::shared_ptr<BufferList>;
    using SendResult = std::function<void(const Buffer::Ptr& buffer, bool send_success)>;

    static Ptr create(List<std::pair<Buffer::Ptr, bool>> list, SendResult cb, bool is_udp);
    BufferList() = default;
    virtual ~BufferList() =  default;

public:
    virtual bool empty() = 0;
    virtual size_t count() = 0;
    virtual ssize_t send(int fd, int flags) = 0;

private:
    ObjectCounter<BufferList> counter_;
};

#endif