/*
*  该文件封装了socket中的发送/接收数据的系统调用
*  BufferSock: 封装目标地址和相应的buffer, udp需要指定目的地址
*  BufferList: 封装了对缓冲区列表的操作的基类, send(), size(), empty()
*  BufferCallBack: 封装对缓冲区列表的操作, 根据发送的结果，调整List中的内容
*  BufferSendMsg: sendmsg()
*  BufferSendTo: sendto()/send()
*  BufferSendMMsg: sendmmsg()
*  SocketRecvFromBuffer: recvfrom()
*  SocketRecvmmsgBuffer: recvmmsg()
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
#include <utility>

#include "buffer.h"
#include "sockutil.h"
#include "utility.h"

namespace xkernel {

// 封装目标地址和相应的buffer
class BufferSock : public Buffer {
public:
    using Ptr = std::shared_ptr<BufferSock>;

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

class BufferCallBack {
public:
    BufferCallBack(List<std::pair<Buffer::Ptr, bool>> list, BufferList::SendResult cb);
    ~BufferCallBack();

public:
    void sendCompleted(bool flag);
    void sendFrontSuccess();

protected:
    BufferList::SendResult cb_;
    List<std::pair<Buffer::Ptr, bool>> pkt_list_;
};

using SocketBuf = iovec;

class BufferSendMsg final : public BufferList,
                            public BufferCallBack {
public:
    using SocketBufVec = std::vector<SocketBuf>;

    BufferSendMsg(List<std::pair<Buffer::Ptr, bool>> list, SendResult cb);
    ~BufferSendMsg() override = default;

public:
    bool empty() override;
    size_t count() override;
    ssize_t send(int fd, int flags) override;

private:
    ssize_t send_l(int fd, int flags);
    void reOffset(size_t n);

private:
    size_t iovec_off_ = 0;  // 当前处理到的iovec索引
    size_t remain_size_ = 0;
    SocketBufVec iovec_;  // 和pkt_list_的内容一一对应
};

class BufferSendTo final : public BufferList,
                           public BufferCallBack {
public:
    BufferSendTo(List<std::pair<Buffer::Ptr, bool>> list, SendResult cb, bool is_udp);
    ~BufferSendTo() override = default;

public:
    bool empty() override;
    size_t count() override;
    ssize_t send(int fd, int flags) override;

private:
    bool is_udp_;
    size_t offset_ = 0;
};

class BufferSendMMsg : public BufferList,
                       public BufferCallBack {
public:
    BufferSendMMsg(List<std::pair<Buffer::Ptr, bool>> list, SendResult cb);
    ~BufferSendMMsg() override = default;

public:
    bool empty() override;
    size_t count() override;
    ssize_t send(int fd, int flags) override;

private:
    ssize_t send_l(int fd, int flags);
    void reOffset(size_t n);

private:
    size_t remain_size_ = 0;
    std::vector<struct iovec> iovec_;
    std::vector<struct mmsghdr> hdrvec_;
};

class SocketRecvmmsgBuffer : public SocketRecvBuffer {
public:
    SocketRecvmmsgBuffer(size_t count, size_t size);

public:
    ssize_t recvFromSocket(int fd, ssize_t &count) override;
    Buffer::Ptr& getBuffer(size_t index) override;
    struct sockaddr_storage& getAddress(size_t index) override;

private:
    size_t size_;
    ssize_t last_count_{0};
    std::vector<struct iovec> iovec_;
    std::vector<struct mmsghdr> mmsgs_;
    std::vector<Buffer::Ptr> buffers_;
    std::vector<struct sockaddr_storage> address_;
};

class SocketRecvFromBuffer : public SocketRecvBuffer {
public:
    SocketRecvFromBuffer(size_t size);

public:
    ssize_t recvFromSocket(int fd, ssize_t& count) override;
    Buffer::Ptr& getBuffer(size_t index) override;
    struct sockaddr_storage& getAddress(size_t index) override;

private:
    void allocBuffer();

private:
    size_t size_;
    Buffer::Ptr buffer_;
    struct sockaddr_storage address_;
};
}  // namespace xkernel
#endif