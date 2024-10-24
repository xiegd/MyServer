/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "BufferSock.h"

#include <assert.h>

#include "Util/logger.h"
#include "Util/uv_errno.h"

// __linux__和__linux都是Linux上编译器自动定义的预定义宏
// 主要确保代码的兼容性和可移植性, 一些旧版本系统上可能不支持，需要自己定义
// 刚才看了下在ubuntu22上直接include socket.h下面的这两个系统调用和那个mmsghdr结构体都有了
#if defined(__linux__) || defined(__linux)

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifndef MSG_WAITFORONE
#define MSG_WAITFORONE 0x10000
#endif

#ifndef HAVE_MMSG_HDR
struct mmsghdr {
    struct msghdr msg_hdr;
    unsigned msg_len;
};
#endif

#ifndef HAVE_SENDMMSG_API
#include <sys/syscall.h>
#include <unistd.h>
static inline int sendmmsg(int fd, struct mmsghdr *mmsg, unsigned vlen,
                           unsigned flags) {
    return syscall(__NR_sendmmsg, fd, mmsg, vlen, flags);
}
#endif

#ifndef HAVE_RECVMMSG_API
#include <sys/syscall.h>  // 提供系统调用号的定义包括__NR_recvmmsg
#include <unistd.h> // 提供syscall声明
static inline int recvmmsg(int fd, struct mmsghdr *mmsg, unsigned vlen,
                           unsigned flags, struct timespec *timeout) {
    return syscall(__NR_recvmmsg, fd, mmsg, vlen, flags, timeout);
}
#endif

#endif  // defined(__linux__) || defined(__linux)

namespace toolkit {

StatisticImp(BufferList)

/////////////////////////////////////// BufferSock//////////////////////////////////////////

BufferSock::BufferSock(Buffer::Ptr buffer, struct sockaddr *addr, int addr_len) {
    if (addr) {
        _addr_len = addr_len ? addr_len : SockUtil::get_sock_len(addr);
        memcpy(&_addr, addr, _addr_len);
    }
    assert(buffer);  // 确保buffer不为空
    _buffer = std::move(buffer);
}

char *BufferSock::data() const { return _buffer->data(); }

size_t BufferSock::size() const { return _buffer->size(); }

const struct sockaddr *BufferSock::sockaddr() const {
    return (struct sockaddr *)&_addr;
}

socklen_t BufferSock::socklen() const { return _addr_len; }

/////////////////////////////////////// BufferCallBack//////////////////////////////////////////
// 管理缓冲区列表的发送操作和回调
// 使用回调支持异步发送
class BufferCallBack {
   public:
    BufferCallBack(List<std::pair<Buffer::Ptr, bool>> list,
                   BufferList::SendResult cb)
        : _cb(std::move(cb)), _pkt_list(std::move(list)) {}

    // 析构时将所有未完成的操作标记为失败
    ~BufferCallBack() { sendCompleted(false); }

    // 通过BufferSendMsg中的send()方法发送完后调用，更新_pkt_list
    void sendCompleted(bool flag) {
        if (_cb) {
            //全部发送成功或失败回调  
            while (!_pkt_list.empty()) {
                _cb(_pkt_list.front().first, flag);
                _pkt_list.pop_front();
            }
        } else {
            _pkt_list.clear();
        }
    }
    // 在发送成功，没有发送完时调用，更新_pkt_list
    void sendFrontSuccess() {
        if (_cb) {
            //发送成功回调  
            _cb(_pkt_list.front().first, true);
        }
        _pkt_list.pop_front();
    }

   protected:
    BufferList::SendResult _cb;  // 回调函数
    List<std::pair<Buffer::Ptr, bool>> _pkt_list;
};

/////////////////////////////////////// BufferSendMsg
#if defined(_WIN32)
using SocketBuf = WSABUF;
#else
using SocketBuf = iovec;
#endif

// 封装使用sendmsg发送数据
class BufferSendMsg final : public BufferList, public BufferCallBack {
   public:
    using SocketBufVec = std::vector<SocketBuf>;

    BufferSendMsg(List<std::pair<Buffer::Ptr, bool>> list, SendResult cb);
    ~BufferSendMsg() override = default;

    bool empty() override;  // 判断是否还有未发送的数据
    size_t count() override;  // 统计剩余需要发送的缓冲区数量
    ssize_t send(int fd, int flags) override;  // 循环调用send_l直到发送完所有数据或失败

   private:
    void reOffset(size_t n);  // 在部分发送成功后更新_iovec_off和_remain_size
    ssize_t send_l(int fd, int flags);

   private:
    size_t _iovec_off = 0;  // 当前处理到的iovec索引
    size_t _remain_size = 0;  // 剩余需要发送的字节数
    SocketBufVec _iovec;  // 存储缓冲区数据的vector<SocketBuf>, 内容和_pkt_list一一对应
};

bool BufferSendMsg::empty() { return _remain_size == 0; }

size_t BufferSendMsg::count() { return _iovec.size() - _iovec_off; }

ssize_t BufferSendMsg::send_l(int fd, int flags) {
    ssize_t n;
#if !defined(_WIN32)
    do {
        struct msghdr msg;
        msg.msg_name = nullptr;
        msg.msg_namelen = 0;
        msg.msg_iov = &(_iovec[_iovec_off]);
        msg.msg_iovlen = _iovec.size() - _iovec_off;
        if (msg.msg_iovlen > IOV_MAX) {
            msg.msg_iovlen = IOV_MAX;
        }
        msg.msg_control = nullptr;
        msg.msg_controllen = 0;
        msg.msg_flags = flags;
        n = sendmsg(fd, &msg, flags);
    } while (-1 == n && UV_EINTR == get_uv_error(true));
#else
    do {
        DWORD sent = 0;
        n = WSASend(fd, const_cast<LPWSABUF>(&_iovec[0]),
                    static_cast<DWORD>(_iovec.size()), &sent,
                    static_cast<DWORD>(flags), 0, 0);
        if (n == SOCKET_ERROR) {
            return -1;
        }
        n = sent;
    } while (n < 0 && UV_ECANCELED == get_uv_error(true));
#endif

    if (n >= (ssize_t)_remain_size) {
        //全部写完了  
        _remain_size = 0;
        sendCompleted(true);
        return n;
    }

    if (n > 0) {
        //部分发送成功  
        reOffset(n);
        return n;
    }

    //一个字节都未发送  
    return n;
}

// 返回发送成功的数据长度，没有发送成功返回-1, flags： 控制发送行为的标志
ssize_t BufferSendMsg::send(int fd, int flags) {
    auto remain_size = _remain_size;  // 记录当前未发送的数据长度
    // 发送数据直到出现错误或者发送完成
    while (_remain_size && send_l(fd, flags) != -1)
        ;

    ssize_t sent = remain_size - _remain_size;  // 比较发送前后的remain_size获取发送数据长度
    if (sent > 0) {
        return sent; //部分或全部发送成功  
    }
    return -1; //一个字节都未发送成功  
}

// 在部分发送成功后更新_iovec_off和_remain_size
void BufferSendMsg::reOffset(size_t n) {
    _remain_size -= n;  // 更新剩余需要发送的数据长度
    size_t offset = 0;
    for (auto i = _iovec_off; i != _iovec.size(); ++i) {
        auto &ref = _iovec[i];
#if !defined(_WIN32)
        offset += ref.iov_len;  // 累加已经发送的数据的长度
#else
        offset += ref.len;
#endif
        if (offset < n) {
            //如果遍历到的这个包属于发送完毕的部分
            sendFrontSuccess();
            continue;
        }
        _iovec_off = i;
        if (offset == n) {
            //这是末尾发送完毕的一个包  
            ++_iovec_off;
            sendFrontSuccess();
            break;
        }
        //这是末尾发送部分成功的一个包  
        size_t remain = offset - n;
#if !defined(_WIN32)
        ref.iov_base = (char *)ref.iov_base + ref.iov_len - remain;
        ref.iov_len = remain;
#else
        ref.buf = (CHAR *)ref.buf + ref.len - remain;
        ref.len = remain;
#endif
        break;
    }
}

// 这里对list使用了移动语义后，list进入了有效但未指定的状态，所以使用已经初始化的_pkt_list来初始化
BufferSendMsg::BufferSendMsg(List<std::pair<Buffer::Ptr, bool>> list,
                             SendResult cb)
    : BufferCallBack(std::move(list), std::move(cb)), _iovec(_pkt_list.size()) {
    auto it = _iovec.begin();
    _pkt_list.for_each([&](std::pair<Buffer::Ptr, bool> &pr) {
#if !defined(_WIN32)
        it->iov_base = pr.first->data();  // 获取char*格式的缓冲区数据数据
        it->iov_len = pr.first->size();  // 获取缓冲区长度
        _remain_size += it->iov_len;
#else
        it->buf = pr.first->data();
        it->len = pr.first->size();
        _remain_size += it->len;
#endif
        ++it;
    });
}

/////////////////////////////////////// BufferSendTo//////////////////////////////////////////

// 封装使用send()/sendto()发送数据
class BufferSendTo final : public BufferList, public BufferCallBack {
   public:
    BufferSendTo(List<std::pair<Buffer::Ptr, bool>> list, SendResult cb,
                 bool is_udp);
    ~BufferSendTo() override = default;

    bool empty() override;
    size_t count() override;
    ssize_t send(int fd, int flags) override;

   private:
    bool _is_udp;
    size_t _offset = 0;
};

BufferSendTo::BufferSendTo(List<std::pair<Buffer::Ptr, bool>> list,
                           BufferList::SendResult cb, bool is_udp)
    : BufferCallBack(std::move(list), std::move(cb)), _is_udp(is_udp) {}

bool BufferSendTo::empty() { return _pkt_list.empty(); }

size_t BufferSendTo::count() { return _pkt_list.size(); }

static inline BufferSock *getBufferSockPtr(std::pair<Buffer::Ptr, bool> &pr) {
    if (!pr.second) {
        return nullptr;
    }
    return static_cast<BufferSock *>(pr.first.get());
}

ssize_t BufferSendTo::send(int fd, int flags) {
    size_t sent = 0;
    ssize_t n;
    // 顺序取出缓冲区列表中的每一个缓冲区，根据_is_udp选择使用send()/sendto()进行发送
    while (!_pkt_list.empty()) {
        auto &front = _pkt_list.front();
        auto &buffer = front.first;
        if (_is_udp) {
            auto ptr = getBufferSockPtr(front);
            n = ::sendto(fd, buffer->data() + _offset, buffer->size() - _offset,
                         flags, ptr ? ptr->sockaddr() : nullptr,
                         ptr ? ptr->socklen() : 0);
        } else {
            n = ::send(fd, buffer->data() + _offset, buffer->size() - _offset,
                       flags);
        }

        if (n >= 0) {
            assert(n);
            _offset += n;
            if (_offset == buffer->size()) {
                sendFrontSuccess();  // 这个缓冲区发送完毕，则从缓冲区列表中删除
                _offset = 0;
            }
            sent += n;
            continue;  // 部分发送成功，因为这里是每次从缓冲区列表的头部取出一个缓冲区进行发送
            // 所以会继续重新发送这个未发送完成的部分
        }

        // n == -1的情况  
        if (get_uv_error(true) == UV_EINTR) {
            //被打断，需要继续发送  
            continue;
        }
        //其他原因导致的send返回-1  
        break;
    }
    return sent ? sent : -1;
}

/////////////////////////////////////// BufferSendMmsg//////////////////////////////////////////

#if defined(__linux__) || defined(__linux)
// 封装使用sendmmsg发送数据
class BufferSendMMsg : public BufferList, public BufferCallBack {
   public:
    BufferSendMMsg(List<std::pair<Buffer::Ptr, bool>> list, SendResult cb);
    ~BufferSendMMsg() override = default;

    bool empty() override;
    size_t count() override;
    ssize_t send(int fd, int flags) override;

   private:
    void reOffset(size_t n);
    ssize_t send_l(int fd, int flags);

   private:
    size_t _remain_size = 0;
    std::vector<struct iovec> _iovec;  // 管理数据
    std::vector<struct mmsghdr> _hdrvec;  // 管理消息结构
};

bool BufferSendMMsg::empty() { return _remain_size == 0; }

size_t BufferSendMMsg::count() { return _hdrvec.size(); }

ssize_t BufferSendMMsg::send_l(int fd, int flags) {
    ssize_t n;
    do {
        n = sendmmsg(fd, &_hdrvec[0], _hdrvec.size(), flags);
    } while (-1 == n && UV_EINTR == get_uv_error(true));

    if (n > 0) {
        //部分或全部发送成功  
        reOffset(n);
        return n;
    }

    //一个字节都未发送  
    return n;
}

ssize_t BufferSendMMsg::send(int fd, int flags) {
    auto remain_size = _remain_size;
    while (_remain_size && send_l(fd, flags) != -1)
        ;
    ssize_t sent = remain_size - _remain_size;
    if (sent > 0) {
        //部分或全部发送成功  
        return sent;
    }
    //一个字节都未发送成功  
    return -1;
}

void BufferSendMMsg::reOffset(size_t n) {
    for (auto it = _hdrvec.begin(); it != _hdrvec.end();) {
        auto &hdr = *it;
        auto &io = *(hdr.msg_hdr.msg_iov);
        assert(hdr.msg_len <= io.iov_len);  // 实际发送的消息长度要小于消息长度
        _remain_size -= hdr.msg_len;
        if (hdr.msg_len == io.iov_len) {
            //这个udp包全部发送成功  
            it = _hdrvec.erase(it);  // 返回移除元素之后元素的迭代器
            sendFrontSuccess();
            continue;
        }
        //部分发送成功  
        io.iov_base = (char *)io.iov_base + hdr.msg_len;
        io.iov_len -= hdr.msg_len;
        break;
    }
}

BufferSendMMsg::BufferSendMMsg(List<std::pair<Buffer::Ptr, bool>> list,
                               SendResult cb)
    : BufferCallBack(std::move(list), std::move(cb)),
      _iovec(_pkt_list.size()),
      _hdrvec(_pkt_list.size()) {
    auto i = 0U;
    // 使用_pkt_list初始化_iovec和_hdrvec, 并计算_remain_size
    _pkt_list.for_each([&](std::pair<Buffer::Ptr, bool> &pr) {
        auto &io = _iovec[i];
        io.iov_base = pr.first->data();
        io.iov_len = pr.first->size();
        _remain_size += io.iov_len;

        auto ptr = getBufferSockPtr(pr);
        auto &mmsg = _hdrvec[i];
        auto &msg = mmsg.msg_hdr;
        mmsg.msg_len = 0;
        msg.msg_name = ptr ? (void *)ptr->sockaddr() : nullptr;
        msg.msg_namelen = ptr ? ptr->socklen() : 0;
        msg.msg_iov = &io;
        msg.msg_iovlen = 1;
        msg.msg_control = nullptr;
        msg.msg_controllen = 0;
        msg.msg_flags = 0;
        ++i;
    });
}

#endif  // defined(__linux__) || defined(__linux)

BufferList::Ptr BufferList::create(List<std::pair<Buffer::Ptr, bool>> list,
                                   SendResult cb, bool is_udp) {
#if defined(_WIN32)
    if (is_udp) {
        // sendto/send 方案，待优化  
        return std::make_shared<BufferSendTo>(std::move(list), std::move(cb),
                                              is_udp);
    }
    // WSASend方案  
    return std::make_shared<BufferSendMsg>(std::move(list), std::move(cb));
#elif defined(__linux__) || defined(__linux)
    if (is_udp) {
        // sendmmsg方案  
        return std::make_shared<BufferSendMMsg>(std::move(list), std::move(cb));
    }
    // sendmsg方案  
    return std::make_shared<BufferSendMsg>(std::move(list), std::move(cb));
#else
    if (is_udp) {
        // sendto/send 方案, 可优化？  
        return std::make_shared<BufferSendTo>(std::move(list), std::move(cb),
                                              is_udp);
    }
    // sendmsg方案  
    return std::make_shared<BufferSendMsg>(std::move(list), std::move(cb));
#endif
}

#if defined(__linux) || defined(__linux__)
// 封装使用recvmmsg接收数据
class SocketRecvmmsgBuffer : public SocketRecvBuffer {
   public:
    SocketRecvmmsgBuffer(size_t count, size_t size)
        : _size(size),
          _iovec(count),
          _mmsgs(count),
          _buffers(count),
          _address(count) {
        for (auto i = 0u; i < count; ++i) {
            auto buf = BufferRaw::create();
            buf->setCapacity(size);

            _buffers[i] = buf;
            auto &mmsg = _mmsgs[i];
            auto &addr = _address[i];
            mmsg.msg_len = 0;
            mmsg.msg_hdr.msg_name = &addr;
            mmsg.msg_hdr.msg_namelen = sizeof(addr);
            mmsg.msg_hdr.msg_iov = &_iovec[i];
            mmsg.msg_hdr.msg_iov->iov_base = buf->data();
            mmsg.msg_hdr.msg_iov->iov_len = buf->getCapacity() - 1;
            mmsg.msg_hdr.msg_iovlen = 1;
            mmsg.msg_hdr.msg_control = nullptr;
            mmsg.msg_hdr.msg_controllen = 0;
            mmsg.msg_hdr.msg_flags = 0;
        }
    }

    ssize_t recvFromSocket(int fd, ssize_t &count) override {
        // 检查buffer是否创建，未创建则创建
        for (auto i = 0; i < _last_count; ++i) {
            auto &mmsg = _mmsgs[i];
            mmsg.msg_hdr.msg_namelen = sizeof(struct sockaddr_storage);
            auto &buf = _buffers[i];
            if (!buf) {
                auto raw = BufferRaw::create();
                raw->setCapacity(_size);
                buf = raw;
                mmsg.msg_hdr.msg_iov->iov_base = buf->data();
            }
        }

        do {
            // 从fd中接收数据到_mmsgs中, 顺序的将收到的消息存在以_mmsg为首地址的mmsghdr数组中
            count = recvmmsg(fd, &_mmsgs[0], _mmsgs.size(), 0, nullptr);
        } while (-1 == count && UV_EINTR == get_uv_error(true));

        _last_count = count;
        if (count <= 0) {
            return count;
        }

        ssize_t nread = 0;  // 收到的数据的长度
        // 根据接收到的消息数量，更新相应收到了数据的缓冲区
        for (auto i = 0; i < count; ++i) {
            auto &mmsg = _mmsgs[i];
            nread += mmsg.msg_len;

            auto buf = std::static_pointer_cast<BufferRaw>(_buffers[i]);
            buf->setSize(mmsg.msg_len);
            buf->data()[mmsg.msg_len] = '\0';
        }
        return nread;
    }
    // 获取指定索引的缓冲区
    Buffer::Ptr &getBuffer(size_t index) override { return _buffers[index]; }

    // 获取指定索引的消息源地址 
    struct sockaddr_storage &getAddress(size_t index) override {
        return _address[index];
    }

   private:
    size_t _size;  // 每个缓冲区的初始化大小
    ssize_t _last_count{0};  // 上次接收的消息的数量
    std::vector<struct iovec> _iovec;  // 描述每个消息的缓冲区
    std::vector<struct mmsghdr> _mmsgs;  // 描述每个消息的消息结构
    std::vector<Buffer::Ptr> _buffers;  // 存储每个消息的缓冲区
    std::vector<struct sockaddr_storage> _address;  // 存储每个消息的源地址
};
#endif

class SocketRecvFromBuffer : public SocketRecvBuffer {
   public:
    SocketRecvFromBuffer(size_t size) : _size(size) {}
    // count: 接收到的数据包数量
    ssize_t recvFromSocket(int fd, ssize_t &count) override {
        ssize_t nread;
        socklen_t len = sizeof(_address);
        if (!_buffer) {
            // 如果_buffer为空，则分配缓冲区, 延迟初始化
            allocBuffer();
        }

        do {
            // 从fd中接收数据到_buffer中, 并获取消息源地址
            nread = recvfrom(fd, _buffer->data(), _buffer->getCapacity() - 1, 0,
                             (struct sockaddr *)&_address, &len);
        } while (-1 == nread && UV_EINTR == get_uv_error(true));

        if (nread > 0) {
            count = 1;
            _buffer->data()[nread] = '\0';  // 在接收数据的末尾添加'\0'
            std::static_pointer_cast<BufferRaw>(_buffer)->setSize(nread);  // resize缓冲区大小
        }
        return nread;
    }

    Buffer::Ptr &getBuffer(size_t index) override { return _buffer; }

    struct sockaddr_storage &getAddress(size_t index) override {
        return _address;
    }

   private:
    // 分配缓冲区
    void allocBuffer() {
        auto buf = BufferRaw::create();
        buf->setCapacity(_size);
        _buffer = std::move(buf);
    }

   private:
    size_t _size;  // 接收缓冲区大小
    Buffer::Ptr _buffer;  // 接收缓冲区
    struct sockaddr_storage _address;  // 消息源地址
};

static constexpr auto kPacketCount = 32;
static constexpr auto kBufferCapacity = 4 * 1024u;

SocketRecvBuffer::Ptr SocketRecvBuffer::create(bool is_udp) {
#if defined(__linux) || defined(__linux__)
    if (is_udp) {
        return std::make_shared<SocketRecvmmsgBuffer>(kPacketCount,
                                                      kBufferCapacity);
    }
#endif
    return std::make_shared<SocketRecvFromBuffer>(kPacketCount *
                                                  kBufferCapacity);
}

}  // namespace toolkit
