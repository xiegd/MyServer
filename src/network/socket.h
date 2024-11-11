#ifndef _SOCKET_H_
#define _SOCKET_H_

#include <exception>
#include <string>
#include <ostream>
#include <memory>
#include <atomic>
#include <sstream>

#include "buffersock.h"
#include "eventpoller.h"
#include "timer.h"
#include "sockutil.h"
#include "utility.h"

namespace xkernel {

// 定义socket的相关标志
#define FLAG_NOSIGNAL MSG_NOSIGNAL
#define FLAG_MORE MSG_MORE
#define FLAG_DONTWAIT MSG_DONTWAIT
#define SOCKET_DEFAULT_FLAGS (FLAG_NOSIGNAL | FLAG_DONTWAIT)
#define SEND_TIME_OUT_SEC 10

enum class ErrorCode {
    Success = 0,   // 成功
    Eof,           // EOF
    Timeout,       // 超时
    Refused,       // 连接被拒绝
    Reset,         // 连接被重置
    Dns,           // DNS 解析失败
    Shutdown,      // 主动关闭
    Other = 0xFF,  // 其他错误
};

class SocketException : public std::exception {
public:
    SocketException(ErrorCode code = ErrorCode::Success, const std::string& msg = "", 
                    int custom_code = 0);
    ~SocketException() override = default;

public:
    void reset(ErrorCode code, const std::string& msg, int custom_code = 0);
    const char* what() const noexcept override;
    ErrorCode getErrCode() const;
    int getCustomCode() const;
    operator bool() const;

private:
    ErrorCode code_;
    int custom_code_ = 0;  // 自定义错误码
    std::string msg_;
};

std::ostream& operator<<(std::ostream& os, const SocketException& ex);

// 封装socket fd和相应的socket类型
class SockNum {
public:
    using Ptr = std::shared_ptr<SockNum>;
    enum class SockType {
        Invalid = -1,
        TCP = 0,
        UDP = 1,
        TCP_Server = 2
    };

    SockNum(int fd, SockType type);
    ~SockNum();

public:
    int rawFd() const;
    SockType type();
    void setConnected();

private:
    int fd_;
    SockType type_;
};

// socket fd的包装类, 封装socket fd和poller, 析构时自动移除事件并关闭fd
class SockFd : public NonCopyable {
public:
    using Ptr = std::shared_ptr<SockFd>;

    SockFd(SockNum::Ptr num, const EventPoller::Ptr& poller);
    SockFd(const SockFd& that, const EventPoller::Ptr& poller);
    ~SockFd();

public:
    void delEvent();
    void setConnected();
    int rawFd() const;
    const SockNum::Ptr& sockNum() const;
    const EventPoller::Ptr& getPoller() const;
    SockNum::SockType type();

private:
    SockNum::Ptr num_;  // 被监听的socket fd的封装
    EventPoller::Ptr poller_;
};

// 互斥锁包装类
template <class Mtx = std::recursive_mutex>
class MutexWrapper {
public:
    MutexWrapper(bool enable) : enable_(enable) {}
    ~MutexWrapper() = default;

public:
    inline void lock() {
        if (enable_) {
            mtx_.lock();
        }
    }

    inline void unlock() {
        if (enable_) {
            mtx_.unlock();
        }
    }

private:
    bool enable_;
    Mtx mtx_;
};

// socket信息接口
class SockInfo {
public:
    SockInfo() = default;
    virtual ~SockInfo() = default;

public:
    virtual std::string getLocalIp() = 0;
    virtual uint16_t getLocalPort() = 0;
    virtual std::string getPeerIp() = 0;
    virtual uint16_t getPeerPort() = 0;
    virtual std::string getIdentifier() const;
};

#define TraceP(ptr)  \
    TraceL << ptr->getIdentifier() << "(" << ptr->getPeerIp() << ":" << ptr->getPeerPort() << ")"
#define DebugP(ptr)  \
    DebugL << ptr->getIdentifier() << "(" << ptr->getPeerIp() << ":" << ptr->getPeerPort() << ")"
#define InfoP(ptr)  \
    InfoL << ptr->getIdentifier() << "(" << ptr->getPeerIp() << ":" << ptr->getPeerPort() << ")"
#define ErrorP(ptr)  \
    ErrorL << ptr->getIdentifier() << "(" << ptr->getPeerIp() << ":" << ptr->getPeerPort() << ")"

class Socket : public std::enable_shared_from_this<Socket>,
               public NonCopyable,
               public SockInfo {
public:
    using Ptr = std::shared_ptr<Socket>;
    using onReadCb = std::function<void(Buffer::Ptr& buf, struct sockaddr* addr, int addr_len)>;
    using onMultiReadCb = std::function<void(Buffer::Ptr* buf, struct sockaddr_storage* addr, size_t count)>;
    using onErrCb = std::function<void(const SockException& err)>;
    using onAcceptCb = std::function<void(Socket::Ptr& sock, std::shared_ptr<void>& complete)>;
    using onFlush = std::function<bool()>;
    using onCreateSocket = std::function<Ptr(const EventPoller::Ptr& poller)>;
    using onSendResult = BufferList::SendResult;

    static Ptr CreateSocket(const EventPoller::Ptr& poller = nullptr, bool enable_mutex = true);
    ~Socket();

public:
    void connect(const std::string& url, uint16_t port, const onErrCb& cb,
                float timeout_sec = 5,
                const std::string& local_ip = "::", uint16_t local_port = 0);
    bool listen(uint16_t port, const std::string& local_ip = "::", int backlog = 1024);
    bool bindUdpSock(uint16_t port, const std::string& local_ip = "::", bool enable_reuse = true);
    bool fromSock(int fd, SockNum::SockType type);
    bool cloneSocket(const Socket& other);  // 从另一个Socket复制， 让一个Socket被多个poller监听
    // 设置事件回调
    void setOnRead(onReadCb cb);
    void setOnMultiRead(onMultiReadCb cb);
    void setOnErr(onErrCb cb);
    void setOnAccept(onAcceptCb cb);
    void setOnFlush(onFlush cb);
    void setOnBeforeAccept(onCreateSocket cb);
    void setOnSendResult(onSendResult cb);
    // 发送数据相关接口
    ssize_t send(const void* buf, size_t size = 0,
                struct sockaddr* addr = nullptr, socklen_t addr_len = 0,
                bool try_flush = true);
    ssize_t send(std::string buf, struct sockaddr* addr = nullptr,
                socklen_t addr_len = 0, bool try_flush = true);
    ssize_t send(Buffer::Ptr buf, struct sockaddr* addr = nullptr, 
                socklen_t addr_len = 0, bool try_flush = true);
    int flushAll();
    bool emitErr(const SockException& err) noexcept;
    void enableRecv(bool enabled);
    int rawFd() const;
    int alive() const;
    SockNum::SockType sockType() const;
    void setSendTimeOutSencond(uint32_t second);
    bool isSocketBusy() const;
    const EventPoller::Ptr& getPoller() const;
    bool bindPeerAddr(const struct sockaddr* dst_addr, socklen_t addr_len = 0, bool soft_bind = false);
    void setSendFlags(int flags = SOCKET_DEFAULT_FLAGS);
    void closeSock(bool close_fd = true);
    size_t getSendBufferCount();
    uint64_t elapsedTimeAfterFlushed();
    int getRecvSpeed();
    int getSendSpeed();
    
    std::string getLocalIp() override;
    uint16_t getLocalPort() override;
    std::string getPeerIp() override;
    uint16_t getPeerPort() override;
    std::string getIdentifier() const override;  // 获取socket对象的内存地址

private:
    Socket(EventPoller::Ptr poller, bool enable_mutex = true);
    void setSock(SockNum::Ptr sock);
    int onAccept(const SockNum::Ptr& sock, int event) noexcept;
    ssize_t onRead(const SockNum::Ptr& sock, const SocketRecvBuffer::Ptr& buffer) noexcept;
    void onWriteAble(const SockNum::Ptr& sock);
    void onConnected(const SockNum::Ptr& sock, const onErrCb& cb);
    void onFlushed();
    void startWriteAbleEvent(const SockNum::Ptr& sock);
    void stopWriteAbleEvent(const SockNum::Ptr& sock);
    bool flushData(const SockNum::Ptr& sock, bool poller_thread);
    bool attachEvent(const SockNum::Ptr& sock);
    ssize_t send_l(Buffer::Ptr buf, bool is_buf_sock, bool try_flush = true);
    void connect_l(const std::string& url, uint16_t port,
                   const onErrCb& con_cb_in, float timeout_sec,
                   const std::string& local_ip, uint16_t local_port);
    bool fromSock_l(SockNum::Ptr sock);

private:
    int sock_flags_ = SOCKET_DEFAULT_FLAGS;                   // socket发送时的flag
    uint32_t max_send_buffer_ms_ = SEND_TIME_OUT_SEC * 1000;  // 最大发送缓存，单位毫秒，距上次发送缓存清空时间不能超过该参数
    std::atomic<bool> enable_recv_{true};                      // 标记是否启用接收监听socket可读事件
    std::atomic<bool> sendable_{true};                        // 标记socket是否可写
    bool err_emit_ = false;                                    // 标记是否已经触发err回调
    bool enable_speed_ = false;                               // 标记是否启用网速统计
    std::shared_ptr<struct sockaddr_storage> udp_send_dst_;   // udp发送目标地址
    BytesSpeed recv_speed_;                                   // 接收速率统计
    BytesSpeed send_speed_;                                   // 发送速率统计
    Timer::Ptr con_timer_;                                    // tcp连接超时定时器
    std::shared_ptr<void> async_con_cb_;                      // tcp连接结果回调对象
    Ticker send_flush_ticker_;                                // 记录上次发送缓存(包括socket写缓存、应用层缓存)清空的计时器
    SockFd::Ptr sock_fd_;                                     // socket fd的抽象类
    EventPoller::Ptr poller_;                                 // 本socket绑定的poller线程，事件触发于此线程
    mutable MutexWrapper<std::recursive_mutex> mtx_sock_fd_;

    onErrCb on_err_;                                        // socket异常事件
    onMultiReadCb on_multi_read_;                           // 收到数据事件
    onFlush on_flush_;                                      // socket缓存清空事件
    onAcceptCb on_accept_;                                  // tcp监听收到accept请求事件
    onCreateSocket on_before_accept_;                       // tcp监听收到accept请求，自定义创建peer
    MutexWrapper<std::recursive_mutex> mtx_event_;          // 设置上述回调的锁

    List<std::pair<Buffer::Ptr, bool>> send_buf_waiting_;   // 一级发送缓存, socket可写时会把一级缓存批量送入二级缓存
    MutexWrapper<std::recursive_mutex> mtx_send_buf_waiting_;  // 一级发送缓存锁
    List<BufferList::Ptr> send_buf_sending_;                   // 二级发送缓存, socket可写时会把二级缓存批量写入socket
    MutexWrapper<std::recursive_mutex> mtx_send_buf_sending_;  // 二级发送缓存锁
    BufferList::SendResult send_result_;                        // 发送buffer结果回调
    ObjectCounter<Socket> statistic_;                           // 对象个数统计
    // 缓存地址，防止tcp reset 导致无法获取对端的地址
    struct sockaddr_storage local_addr_;
    struct sockaddr_storage peer_addr_;
};

class SockSender {
public:
    SockSender() = default;
    virtual ~SockSender() = default;
    virtual ssize_t send(Buffer::Ptr buf) = 0;
    virtual void shutdown(const SockException& ex = SockException(ErrorCode::Shutdown, "self shutdown")) = 0;
    SockSender& operator<<(Buffer::Ptr buf);
    SockSender& operator<<(std::string buf);
    SockSender& operator<<(const char* buf);
    // 发送其他类型数据
    template <typename T>
    SockSender& operator<<(T&& buf) {
        std::ostringstream ss;
        ss << std::forward<T>(buf);
        send(ss.str());
        return *this;
    }
    ssize_t send(std::string buf);
    ssize_t send(const char* buf, size_t size = 0);
};

class SocketHelper : public SockSender,
                     public SockInfo,
                     public TaskExecutorInterface,
                     public std::enable_shared_from_this<SocketHelper> {
public:
    using Ptr = std::shared_ptr<SocketHelper>;
    
    SocketHelper(const Socket::Ptr& sock);
    ~SocketHelper() override = default;

public:
    const EventPoller::Ptr& getPoller() const;
    void setSendFlushFlag(bool try_flush);
    void setSendFlags(int flags);
    bool isSocketBusy() const;
    void setOnCreateSocket(Socket::onCreateSocket cb);
    Socket::Ptr createSocket();
    const Socket::Ptr& getSock() const;
    int flushAll();
    virtual bool overSsl() const;
    // 重载SockInfo接口
    std::string getLocalIp() override;
    uint16_t getLocalPort() override;
    std::string getPeerIp() override;
    uint16_t getPeerPort() override;
    // 重载TaskExecutorInterface接口
    Task::Ptr async(TaskIn task, bool may_sync = true) override;
    Task::Ptr asyncFirst(TaskIn task, bool may_sync = true) override;
    // 重载SockSender接口
    using SockSender::send;
    ssize_t send(Buffer::Ptr buf) override;
    void shutdown(const SockException& ex = SockException(ErrorCode::Shutdown, "self shutdown")) override;
    void safeShutdown(const SockException& ex = SockException(ErrorCode::Shutdown, "self shutdown"));

    virtual void onRecv(const Buffer::Ptr& buf) = 0;
    virtual void onErr(const SockException& err) = 0;
    virtual void onFlush() = 0;
    virtual void onManager() = 0;

protected:
    void setPoller(const EventPoller::Ptr& poller);
    void setSock(const Socket::Ptr& sock);

private:
    bool try_flush_ = true;
    Socket::Ptr sock_;
    EventPoller::Ptr poller_;
    Socket::onCreateSocket on_create_socket_;
};

}  // namespace xkernel
#endif // __SOCKET_H__