#include <cassert>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <utility>

#include "buffersock.h"
#include "logger.h"
#include "uv_errno.h"
#include "sockutil.h"
#include "utility.h"
#include "buffer.h"

namespace xkernel {

STATISTIC_IMPL(BufferList)

///////////////////////////////////// BufferSock //////////////////////////////////////

BufferSock::BufferSock(Buffer::Ptr buffer, struct sockaddr* addr, int addr_len) {
    if (addr) {
        addr_len_ = addr_len ? addr_len : SockUtil::getSockLen(addr);
        memcpy(&addr_, addr, addr_len_);
    }
    assert(buffer);
    buffer_ = std::move(buffer);
}

char* BufferSock::data() const { return buffer_->data(); }
size_t BufferSock::size() const { return buffer_->size(); }
const struct sockaddr* BufferSock::sockaddr() const { return reinterpret_cast<const struct sockaddr*>(&addr_); }
socklen_t BufferSock::socklen() const { return addr_len_; }
std::string BufferSock::toString() const { return buffer_->toString(); }
size_t BufferSock::getCapacity() const { return buffer_->getCapacity(); }

///////////////////////////////////// SocketRecvBuffer //////////////////////////////////////

static constexpr auto kPacketCount = 32;
static constexpr auto kBufferCapacity = 4 * 1024u;

SocketRecvBuffer::Ptr SocketRecvBuffer::create(bool is_udp) {
    if (is_udp) {
        return std::make_shared<SocketRecvmmsgBuffer>(kPacketCount, kBufferCapacity);
    }
    return std::make_shared<SocketRecvFromBuffer>(kPacketCount * kBufferCapacity);
}

///////////////////////////////////// BufferList //////////////////////////////////////

BufferList::Ptr BufferList::create(List<std::pair<Buffer::Ptr, bool>> list, SendResult cb, bool is_udp) {
    if (is_udp) {
        return std::make_shared<BufferSendMMsg>(std::move(list), std::move(cb));
    }
    return std::make_shared<BufferSendMsg>(std::move(list), std::move(cb));
}

///////////////////////////////////// BufferCallBack //////////////////////////////////////

BufferCallBack::BufferCallBack(List<std::pair<Buffer::Ptr, bool>> list, BufferList::SendResult cb)
    : cb_(std::move(cb)), pkt_list_(std::move(list)) {}

BufferCallBack::~BufferCallBack() { sendCompleted(false); }

void BufferCallBack::sendCompleted(bool flag) {
    if (cb_) {
        while (!pkt_list_.empty()) {
            cb_(pkt_list_.front().first, flag);
            pkt_list_.pop_front();
        }
    } else {
        pkt_list_.clear();
    }
}

void BufferCallBack::sendFrontSuccess() {
    if (cb_) {
        cb_(pkt_list_.front().first, true);
    }
    pkt_list_.pop_front();
}

///////////////////////////////////// BufferSendMsg //////////////////////////////////////

BufferSendMsg::BufferSendMsg(List<std::pair<Buffer::Ptr, bool>> list, SendResult cb) 
    : BufferCallBack(std::move(list), std::move(cb)), iovec_(pkt_list_.size()){
        auto it = iovec_.begin();
        pkt_list_.forEach([&](std::pair<Buffer::Ptr, bool>& pr) {
            it->iov_base = pr.first->data();
            it->iov_len = pr.first->size();
            remain_size_ += it->iov_len;
            ++it;
        });
}

bool BufferSendMsg::empty() { return remain_size_ == 0; }
size_t BufferSendMsg::count() { return iovec_.size() - iovec_off_; }

ssize_t BufferSendMsg::send(int fd, int flags) {
    auto remain_size = remain_size_;
    while (remain_size && send_l(fd, flags) != -1)
        ;
    ssize_t sent = remain_size - remain_size_;
    if (sent > 0) {
        return sent;
    }
    return -1;
}

ssize_t BufferSendMsg::send_l(int fd, int flags) {
    ssize_t n;
    do {
        struct msghdr msg;
        msg.msg_name = nullptr;
        msg.msg_namelen = 0;
        msg.msg_iov = &(iovec_[iovec_off_]);
        msg.msg_iovlen = iovec_.size() - iovec_off_;
        if (msg.msg_iovlen > IOV_MAX) {
            msg.msg_iovlen = IOV_MAX;
        }
        msg.msg_control = nullptr;
        msg.msg_controllen = 0;
        msg.msg_flags = flags;
        n = sendmsg(fd, &msg, flags);
    } while (-1 == n && UV_EINTR == get_uv_error(true));

    if (n >= static_cast<ssize_t>(remain_size_)) {
        remain_size_ = 0;
        sendCompleted(true); // 全部发送成功   
        return n;
    }
    if (n > 0) {
        reOffset(n);  // 部分发送成功重新设置iovec_off
        return n;
    }
    return n;
}

void BufferSendMsg::reOffset(size_t n) {
    remain_size_ -= n;
    size_t offset = 0;
    for (auto i = iovec_off_; i != iovec_.size(); ++i) {
        auto& ref = iovec_[i];
        offset += ref.iov_len;  // 统计已经发送部分的长度
        // 如果这个包属于发送完毕的部分
        if (offset < n) {
            sendFrontSuccess();
            continue;
        }
        iovec_off_ = i;
        // 如果末尾的包刚好发送完毕
        if (offset == n) {
            ++iovec_off_;
            sendFrontSuccess();
            break;
        }
        // 更新发送了但没发送完的缓冲区的指针和长度
        size_t remain = offset - n;
        ref.iov_base = static_cast<char*>(ref.iov_base) + ref.iov_len - remain;
        ref.iov_len = remain;
        break;
    }
}

///////////////////////////////////// BufferSendTo //////////////////////////////////////

BufferSendTo::BufferSendTo(List<std::pair<Buffer::Ptr, bool>> list, SendResult cb, bool is_udp)
    : BufferCallBack(std::move(list), std::move(cb)), is_udp_(is_udp) {}

bool BufferSendTo::empty() { return pkt_list_.empty(); }
size_t BufferSendTo::count() { return pkt_list_.size(); }

static inline BufferSock* getBufferSockPtr(std::pair<Buffer::Ptr, bool>& pr) {
    if (!pr.second) {
        return nullptr;
    }
    return static_cast<BufferSock*>(pr.first.get());
}

ssize_t BufferSendTo::send(int fd, int flags) {
    size_t sent = 0;
    ssize_t n;
    // 顺序取出缓冲区列表中的每一个缓冲区，根据_is_udp选择使用send()/sendto()进行发送
    while (!pkt_list_.empty()) {
        auto& front = pkt_list_.front();
        auto& buffer = front.first;
        if (is_udp_) {
            auto ptr = getBufferSockPtr(front);
            n = ::sendto(fd, buffer->data() + offset_, buffer->size() - offset_,
                         flags, ptr ? ptr->sockaddr() : nullptr,
                         ptr ? ptr->socklen() : 0);
        } else {
            n = ::send(fd, buffer->data() + offset_, buffer->size() - offset_, flags);
        }

        if (n >= 0) {
            assert(n);
            offset_ += n;
            if (offset_ == buffer->size()) {
                sendFrontSuccess();  // 这个缓冲区发送完毕，则从缓冲区列表中删除
                offset_ = 0;
            }
            sent += n;
            continue;  // 部分发送成功, 重新发送
        }

        if (get_uv_error(true) == UV_EINTR) {
            continue;  // 被打断，需要继续发送
        }
        break;  // 其他原因导致的send返回-1
    }
    return sent ? sent : -1;
}

///////////////////////////////////// BufferSendMMsg //////////////////////////////////////

BufferSendMMsg::BufferSendMMsg(List<std::pair<Buffer::Ptr, bool>> list, SendResult cb) 
    : BufferCallBack(std::move(list), std::move(cb)), iovec_(pkt_list_.size()), hdrvec_(pkt_list_.size()) {
        auto i = 0U;
        pkt_list_.forEach([&](std::pair<Buffer::Ptr, bool>& pr) {
            auto& io = iovec_[i];
            io.iov_base = pr.first->data();
            io.iov_len = pr.first->size();
            remain_size_ += io.iov_len;
            // 填充mmsg结构体
            auto ptr = getBufferSockPtr(pr);
            auto& mmsg = hdrvec_[i];
            auto& msg = mmsg.msg_hdr;
            mmsg.msg_len = 0;
            msg.msg_name = ptr ? (void*)ptr->sockaddr() : nullptr;
            msg.msg_namelen = ptr ? ptr->socklen() : 0;
            msg.msg_iov = &io;  // 填充消息结构体
            msg.msg_iovlen = 1;
            msg.msg_control = nullptr;
            msg.msg_controllen = 0;
            msg.msg_flags = 0;
            ++i;
        });
}

bool BufferSendMMsg::empty() { return remain_size_ == 0; }
size_t BufferSendMMsg::count() { return hdrvec_.size(); }

ssize_t BufferSendMMsg::send(int fd, int flags) {
    auto remain_size = remain_size_;
    while (remain_size_ && send_l(fd, flags) != -1)
        ;
    ssize_t sent = remain_size - remain_size_;
    if (sent > 0) {
        return sent;
    }
    return -1;
}

ssize_t BufferSendMMsg::send_l(int fd, int flags) {
    ssize_t n;
    do {
        // 传入的是mmsghdr的数组，同时处理多个消息
        n = sendmmsg(fd, &hdrvec_[0], hdrvec_.size(), flags);
    } while (-1 == n && UV_EINTR == get_uv_error(true));

    if (n > 0) {
        reOffset(n);
        return n;
    }
    return n;
}

void BufferSendMMsg::reOffset(size_t n) {
    for (auto it = hdrvec_.begin(); it != hdrvec_.end(); ) {
        auto& hdr = *it;
        auto& io = *(hdr.msg_hdr.msg_iov);
        assert(hdr.msg_len <= io.iov_len);
        remain_size_ -= hdr.msg_len;
        if (hdr.msg_len == io.iov_len) {
            // 这个udp包全部发送成功
            it = hdrvec_.erase(it);  // 返回移除元素之后元素的迭代器
            sendFrontSuccess();
            continue;
        }
        // 部分发送成功, 说明这个缓冲区没有发送完毕, 更新缓冲区指针和长度
        io.iov_base = static_cast<char*>(io.iov_base) + hdr.msg_len;
        io.iov_len -= hdr.msg_len;
        break;
    }
}

///////////////////////////////////// SocketRecvmmsgBuffer //////////////////////////////////////

SocketRecvmmsgBuffer::SocketRecvmmsgBuffer(size_t count, size_t size) 
    : size_(size), iovec_(count), mmsgs_(count), buffers_(count), address_(count) {
    for (auto i = 0u;  i < count; ++i) {
        auto buf = BufferRaw::create();
        buf->setCapacity(size);

        buffers_[i] = buf;
        auto& mmsg = mmsgs_[i];
        auto& addr = address_[i];
        mmsg.msg_len = 0;
        mmsg.msg_hdr.msg_name = &addr;
        mmsg.msg_hdr.msg_namelen = sizeof(addr);
        mmsg.msg_hdr.msg_iov = &iovec_[i];
        mmsg.msg_hdr.msg_iov->iov_base = buf->data();
        mmsg.msg_hdr.msg_iov->iov_len = buf->getCapacity() - 1;
        mmsg.msg_hdr.msg_iovlen = 1;
        mmsg.msg_hdr.msg_control = nullptr;
        mmsg.msg_hdr.msg_controllen = 0;
        mmsg.msg_hdr.msg_flags = 0;
    }
}

ssize_t SocketRecvmmsgBuffer::recvFromSocket(int fd, ssize_t& count) {
    for (auto i = 0u; i < last_count_; ++i) {
        auto& mmsg = mmsgs_[i];
        mmsg.msg_hdr.msg_namelen = sizeof(struct sockaddr_storage);
        auto& buf = buffers_[i];
        if (!buf) {
            auto raw = BufferRaw::create();
            raw->setCapacity(size_);
            buf = raw;
            mmsg.msg_hdr.msg_iov->iov_base = buf->data();
        }
    }
    do {
        // 将接收到的count条消息依次存到mmsgs_数组中
        count = recvmmsg(fd, &mmsgs_[0], mmsgs_.size(), 0, nullptr);
    } while (-1 == count && UV_EINTR == get_uv_error(true));

    last_count_ = count;
    if (count <= 0) {
        return count;
    }

    ssize_t nread = 0;
    // 根据接收消息的总数更新相应的缓冲区
    for (auto i = 0u; i < count; ++i) {
        auto& mmsg = mmsgs_[i];
        nread += mmsg.msg_len;

        auto buf = std::static_pointer_cast<BufferRaw>(buffers_[i]);
        buf->setSize(mmsg.msg_len);
        buf->data()[mmsg.msg_len] = '\0';
    }
    return nread;
}

Buffer::Ptr& SocketRecvmmsgBuffer::getBuffer(size_t index) { return buffers_[index]; }
struct sockaddr_storage& SocketRecvmmsgBuffer::getAddress(size_t index) { return address_[index]; }

///////////////////////////////////// SocketRecvFromBuffer //////////////////////////////////////

SocketRecvFromBuffer::SocketRecvFromBuffer(size_t size) : size_(size) {}

ssize_t SocketRecvFromBuffer::recvFromSocket(int fd, ssize_t& count) {
    ssize_t nread;
    socklen_t len = sizeof(address_);
    if (!buffer_) {
        allocBuffer();
    }

    do {
        nread = recvfrom(fd, buffer_->data(), buffer_->getCapacity() - 1, 0,
                        reinterpret_cast<struct sockaddr*>(&address_), &len);
    } while (-1 == nread && UV_EINTR == get_uv_error(true));

    if (nread > 0) {
        count = 1;
        buffer_->data()[nread] = '\0';
        std::static_pointer_cast<BufferRaw>(buffer_)->setSize(nread);
    }
    return nread;
}

Buffer::Ptr& SocketRecvFromBuffer::getBuffer(size_t index) { return buffer_; }
struct sockaddr_storage& SocketRecvFromBuffer::getAddress(size_t index) { return address_; }

void SocketRecvFromBuffer::allocBuffer() {
    auto buf = BufferRaw::create();
    buf->setCapacity(size_);
    buffer_ = std::move(buf);
}

} // namespace xkernel