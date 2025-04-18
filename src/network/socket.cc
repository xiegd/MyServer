#include "socket.h"

#include <unistd.h>

#include "threadpool.h"
#include "uv_errno.h"
#include "buffer.h"
#include "session.h"

namespace xkernel {

STATISTIC_IMPL(Socket)

static SockException toSockException(int error) {
    switch (error) {
        case 0:
        case UV_EAGAIN:
            return SockException(ErrorCode::Success, "success");
        case UV_ECONNREFUSED:
            return SockException(ErrorCode::Refused, uv_strerror(error));
        case UV_ETIMEDOUT:
            return SockException(ErrorCode::Timeout, uv_strerror(error));
        default:
            return SockException(ErrorCode::Other, uv_strerror(error));
    }
}

static SockException getSockErr(int sock, bool try_errno = true) {
    int error = 0, len = sizeof(int);
    getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&error, (socklen_t*)&len);
    if (error == 0) {
        if (try_errno) {
            error = get_uv_error(true);
        } else {
            error = uv_translate_posix_error(error);
        }
    }
    return toSockException(error);
}


//////////////////////////////// SockException /////////////////////////////
SockException::SockException(ErrorCode code, const std::string& msg, int custom_code) 
    : code_(code), custom_code_(custom_code), msg_(msg) {}

void SockException::reset(ErrorCode code, const std::string& msg, int custom_code) {
    code_ = code;
    custom_code_ = custom_code;
    msg_ = msg;
}

const char* SockException::what() const noexcept { return msg_.c_str(); }

ErrorCode SockException::getErrCode() const { return code_; }

int SockException::getCustomCode() const { return custom_code_; }

SockException::operator bool() const { return code_ != ErrorCode::Success; }

//////////////////////////////// SockNum //////////////////////////////////////

SockNum::SockNum(int fd, SockType type) : fd_(fd), type_(type) {}

SockNum::~SockNum() {
    ::shutdown(fd_, SHUT_RDWR);
    close(fd_);
}

int SockNum::rawFd() const { return fd_; }

SockNum::SockType SockNum::type() { return type_; }

void SockNum::setConnected() { /* 为IOS上的处理预留 */}

//////////////////////////////// SockFd //////////////////////////////////////

SockFd::SockFd(SockNum::Ptr num, const EventPoller::Ptr& poller) 
    : num_(std::move(num)), poller_(poller) {}

SockFd::SockFd(const SockFd& that, const EventPoller::Ptr& poller) 
    : num_(that.num_), poller_(poller) {
    if (poller_ == that.poller_) {
        throw std::invalid_argument("Copy a SockFD with same poller");
    }
}

SockFd::~SockFd() { delEvent(); }


void SockFd::delEvent() {
    // 关闭fd在type_的析构时进行
    if (poller_) {
        auto num = num_;
        poller_->delEvent(num->rawFd(), [num](bool){});
        poller_ = nullptr;
    }
}

void SockFd::setConnected() { num_->setConnected(); }  // 只针对IOS

int SockFd::rawFd() const { return num_->rawFd(); }

const SockNum::Ptr& SockFd::sockNum() const { return num_; }

const EventPoller::Ptr& SockFd::getPoller() const { return poller_; }

SockNum::SockType SockFd::type() { return num_->type(); }

//////////////////////////////// SockInfo ////////////////////////////////////

std::string SockInfo::getIdentifier() const { return ""; }

//////////////////////////////// Socket //////////////////////////////////////

/*
    brief: 创建socket实例(静态方法)，如果传入了poller_in，则使用传入的poller_in，
        否则在EventPollerPool中选择负载最小的poller
    param:
        poller_in: 事件轮询器, 默认为nullptr
        enable_mutex: 是否启用互斥锁, 默认为true
*/
Socket::Ptr Socket::createSocket(const EventPoller::Ptr& poller_in, bool enable_mutex) {
    auto poller = poller_in ? poller_in : EventPollerPool::Instance().getPoller();
    std::weak_ptr<EventPoller> weak_poller = poller;
    return Socket::Ptr(new Socket(poller, enable_mutex), [weak_poller](Socket* ptr) {
        if (auto poller = weak_poller.lock()) {
            poller->async([ptr]() { delete ptr; });
        } else {
            delete ptr;
        }
    });
}

Socket::Socket(EventPoller::Ptr poller, bool enable_mutex)
    : poller_(std::move(poller)),
      mtx_sock_fd_(enable_mutex),
      mtx_event_(enable_mutex),
      mtx_send_buf_waiting_(enable_mutex),
      mtx_send_buf_sending_(enable_mutex) {
    setOnRead(nullptr);
    setOnErr(nullptr);
    setOnAccept(nullptr);
    setOnFlush(nullptr);
    setOnBeforeAccept(nullptr);
    setOnSendResult(nullptr);
}

Socket::~Socket() { closeSock(); }

void Socket::setOnRead(onReadCb cb) {
    onMultiReadCb cb2;
    if (cb) {
        cb2 = [cb](Buffer::Ptr* buf, struct sockaddr_storage* addr, size_t count) {
            for (size_t i = 0u; i < count; ++i) {
                cb(buf[i], reinterpret_cast<sockaddr*>(addr + i), sizeof(sockaddr_storage));
            }
        };
    }
    setOnMultiRead(std::move(cb2));
}

void Socket::setOnMultiRead(onMultiReadCb cb) {
    std::lock_guard<decltype(mtx_event_)> lock(mtx_event_);
    if (cb) {
        on_multi_read_ = std::move(cb);
    } else {
        on_multi_read_ = [](Buffer::Ptr* buf, struct sockaddr_storage* addr, size_t count) {
            for (size_t i = 0u; i < count; ++i) {
                WarnL << "Socket not set read callback, data ignored: " << buf[i]->size();
            }
        };
    }
}

void Socket::setOnErr(onErrCb cb) {
    std::lock_guard<decltype(mtx_event_)> lock(mtx_event_);
    if (cb) {
        on_err_ = std::move(cb);
    } else {
        on_err_ = [](const SockException& err) {
            WarnL << "Socket not set err callback, err: " << err;
        };
    }
}

/*
    brief: 设置accept事件回调, 对应在TcpServer::setupEvent()里设置cb，然后调用这个方法
        将新连接分发到对应的线程处理
    param:
        cb: accept事件回调函数
*/
void Socket::setOnAccept(onAcceptCb cb) {
    std::lock_guard<decltype(mtx_event_)> lock(mtx_event_);
    if (cb) {
        on_accept_ = std::move(cb);
    } else {
        on_accept_ = [](Socket::Ptr& sock, std::shared_ptr<void>& complete) {
            WarnL << "Socket not set accept callback, peer fd: " << sock->rawFd();
        };
    }
}

void Socket::setOnFlush(onFlush cb) {
    std::lock_guard<decltype(mtx_event_)> lock(mtx_event_);
    if (cb) {
        on_flush_ = std::move(cb);
    } else {
        on_flush_ = []() { return true; };
    }
}

/*
    brief: 设置before accept事件回调, 即TcpServer::onBeforeAcceptConnection回调
        在accept前调用，创建socket
    param:
        cb: before accept事件回调函数
*/
void Socket::setOnBeforeAccept(onCreateSocket cb) {
    std::lock_guard<decltype(mtx_event_)> lock(mtx_event_);
    if (cb) {
        on_before_accept_ = std::move(cb);
    } else {
        on_before_accept_ = [](const EventPoller::Ptr& poller) {
            return nullptr;
        };
    }
}

void Socket::setOnSendResult(onSendResult cb) {
    std::lock_guard<decltype(mtx_event_)> lock(mtx_event_);
    send_result_ = std::move(cb);
}

/*
    主动连接
*/
void Socket::connect(const std::string& url, uint16_t port, const onErrCb& con_cb_in, 
            float timeout_sec, const std::string& local_ip, uint16_t local_port) {
    std::weak_ptr<Socket> weak_self = shared_from_this();
    poller_->async([=]() {
        if (auto strong_self = weak_self.lock()) {
            strong_self->connect_l(url, port, con_cb_in, timeout_sec, local_ip, local_port);
        }
    });
}

void Socket::connect_l(const std::string& url, uint16_t port,
                       const onErrCb& con_cb_in, float timeout_sec,
                       const std::string& local_ip, uint16_t local_port) {
    closeSock();  // 关闭当前socket
    std::weak_ptr<Socket> weak_self = shared_from_this();
    // 连接回调, 外层回调，负责进行清理和调用外部连接回调函数
    auto con_cb = [con_cb_in, weak_self](const SockException& err) {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return ;
        }
        strong_self->async_con_cb_ = nullptr;  // tcp连接结果回调对象
        strong_self->con_timer_ = nullptr;  // tcp连接超时定时器
        if (err) {
            strong_self->setSock(nullptr);
        }
        con_cb_in(err);  // 调用外部传入的连接回调函数
    };

    // 异步连接回调, 内层回调，设置Socket相关信息，注册事件监听
    auto async_con_cb = std::make_shared<std::function<void(const SockNum::Ptr&)>>(
        [weak_self, con_cb](const SockNum::Ptr& sock) {
            auto strong_self = weak_self.lock();
            if (!sock || !strong_self) {
                con_cb(SockException(ErrorCode::Dns, get_uv_errmsg(true)));
                return ;
            }
            // 添加对fd的事件监听，对应事件发生时调用回调执行连接时的回调
            int result = strong_self->poller_->addEvent(
                sock->rawFd(), EventPoller::Poll_Event::Write_Event | EventPoller::Poll_Event::Error_Event,
                [weak_self, sock, con_cb](EventPoller::Poll_Event event) {
                    if (auto strong_self = weak_self.lock()) {
                        strong_self->onConnected(sock, con_cb);
                    }
                });

            if (result == -1) {
                con_cb(SockException(ErrorCode::Other, 
                                     std::string("add event to poller failed when start connect: ") + get_uv_errmsg()));
            } else {
                strong_self->setSock(sock);
            }
        });

    // 连接超时定时器
    con_timer_ = std::make_shared<Timer>(timeout_sec, [weak_self, con_cb]() {
            con_cb(SockException(ErrorCode::Timeout, uv_strerror(UV_ETIMEDOUT)));
            return false;
        }, poller_);

    // 进行连接
    if (SockUtil::isIP(url.data())) {
        auto fd = SockUtil::connect(url.data(), port, true, local_ip.data(), local_port);
        (*async_con_cb)(fd == -1 ? nullptr : std::make_shared<SockNum>(fd, SockNum::SockType::TCP));
    } else {
        auto poller = poller_;
        std::weak_ptr<std::function<void(const SockNum::Ptr&)>> weak_task = async_con_cb;
        WorkThreadPool::Instance().getExecutor()->async([url, port, local_ip, local_port, weak_task, poller]() {
            // 阻塞式dns解析放在后台线程执行
            int fd = SockUtil::connect(url.data(), port, true, local_ip.data(), local_port);
            auto sock = fd == -1 ? nullptr : std::make_shared<SockNum>(fd, SockNum::SockType::TCP);
            // 如果weak_task没有被释放，则调用回调函数
            poller->async([sock, weak_task]() {
                if (auto strong_task = weak_task.lock()) {
                    (*strong_task)(sock);
                }
            });
        });
        async_con_cb_ = async_con_cb;
    }
}

void Socket::onConnected(const SockNum::Ptr& sock, const onErrCb& cb) {
    auto err = getSockErr(sock->rawFd(), false);
    ///
    // TraceL << "err: " << err;
    if (err.getErrCode() != ErrorCode::Success) {
        cb(err);
        return ;
    }
    setSock(sock);
    poller_->delEvent(sock->rawFd(), [sock](bool) {});
    if (!attachEvent(sock)) {
        cb(SockException(ErrorCode::Other, "add event to poller failed when connected"));
        return ;
    }
    {
        std::lock_guard<decltype(mtx_sock_fd_)> lock(mtx_sock_fd_);
        if (sock_fd_) {
            sock_fd_->setConnected();  // 只针对ios
        }
    }
    // 连接成功
    cb(err);
}

/*
    brief: 创建socket后，根据socket类型，注册对应的事件监听, 设置相应的回调
*/
bool Socket::attachEvent(const SockNum::Ptr& sock) {
    std::weak_ptr<Socket> weak_self = shared_from_this();
    // tcp server
    if (sock->type() == SockNum::SockType::TCP_Server) {
        // 添加对fd的监听, 监听可读事件时，调用onAccept回调中accept()返回的socketfd进行实际的数据收发
        auto result = poller_->addEvent(
            sock->rawFd(), 
            EventPoller::Poll_Event::Read_Event | EventPoller::Poll_Event::Error_Event,
            [weak_self, sock](EventPoller::Poll_Event event) {
                if (auto strong_self = weak_self.lock()) {
                    strong_self->onAccept(sock, event);
                }
            });
        return -1 != result;
    }

    // 其他类型的连接
    auto read_buffer = poller_->getSharedBuffer(sock->type() == SockNum::SockType::UDP);  // 获取共享的缓冲区
    auto result = poller_->addEvent(
        sock->rawFd(),
        EventPoller::Poll_Event::Read_Event | EventPoller::Poll_Event::Write_Event | EventPoller::Poll_Event::Error_Event,
        [weak_self, sock, read_buffer](EventPoller::Poll_Event event) {
            auto strong_self = weak_self.lock();
            if (!strong_self) {
                return ;
            }
            if (!(event & EventPoller::Poll_Event::Read_Event)) {
                strong_self->onRead(sock, read_buffer);
            }
            if (!(event & EventPoller::Poll_Event::Write_Event)) {
                strong_self->onWriteAble(sock);
            }
            if (!(event & EventPoller::Poll_Event::Error_Event)) {
                if (sock->type() == SockNum::SockType::UDP) {
                    // udp ignore error
                } else {
                    strong_self->emitErr(getSockErr(sock->rawFd()));
                }
            }
        });
    return -1 != result;
}

ssize_t Socket::onRead(const SockNum::Ptr& sock, const SocketRecvBuffer::Ptr& buffer) noexcept {
    ssize_t ret = 0, nread = 0, count = 0;  // count指示是否接收了数据, nread指示接收到的数据长度
    InfoL << "onRead() callback";
    while (enable_recv_) {
        nread = buffer->recvFromSocket(sock->rawFd(), count);
        if (nread == 0) {
            if (sock->type() == SockNum::SockType::TCP) {
                emitErr(SockException(ErrorCode::Eof, "end of file"));
            } else {
                WarnL << "Recv eof on udp socket[" << sock->rawFd() << "]";
            }
            return ret;
        }
        if (nread == -1) {
            auto err = get_uv_error(true);
            if (err != UV_EAGAIN) {
                // EAGAIN: 非阻塞模式下，没有数据可读
                if (sock->type() == SockNum::SockType::TCP) {
                    emitErr(toSockException(err));
                } else {
                    WarnL << "Recv err on udp socket[" << sock->rawFd() << "]: " << uv_strerror(err);
                }
            }
            return ret;
        }
        ret += nread;
        if (enable_speed_) {
            recv_speed_ += nread;
        }
        auto& buf = buffer->getBuffer(0);
        auto& addr = buffer->getAddress(0);
        try {
            std::lock_guard<decltype(mtx_event_)> lock(mtx_event_);
            on_multi_read_(&buf, &addr, count);  // 调用server设置的on_multi_read_回调，进行后续的处理
        } catch (std::exception& ex) {
            ErrorL << "Exception occured when emit on_read: " << ex.what();
        }
    }
    InfoL << "onRead() end";
    return 0;  // 没有开启enable_recv
}

bool Socket::emitErr(const SockException& err) noexcept {
    if (err_emit_) {
        return true;
    }
    err_emit_ = true;
    std::weak_ptr<Socket> weak_self = shared_from_this();
    poller_->async([weak_self, err]() {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return ;
        }
        std::lock_guard<decltype(strong_self->mtx_event_)> lock(strong_self->mtx_event_);
        try {
            strong_self->on_err_(err);
        } catch (std::exception& ex) {
            ErrorL << "Exception occured when emit on_err: " << ex.what();
        }
        strong_self->closeSock(false);  // 延后关闭socket，只移除其io事件，防止Session对象析构时获取fd相关信息失败
    });
    return true;
}

bool Socket::listen(uint16_t port, const std::string& local_ip, int backlog) {
    closeSock();
    int fd = SockUtil::listen(port, local_ip.data(), backlog);
    if (fd == -1) {
        return false;
    }
    return fromSock_l(std::make_shared<SockNum>(fd, SockNum::SockType::TCP_Server));
}

bool Socket::bindUdpSock(uint16_t port, const std::string& local_ip, bool enable_reuse) {
    closeSock();
    int fd = SockUtil::bindUdpSock(port, local_ip.data(), enable_reuse);
    if (fd == -1) {
        return false;
    }
    return fromSock_l(std::make_shared<SockNum>(fd, SockNum::SockType::UDP));
}

bool Socket::fromSock(int fd, SockNum::SockType type) {
    closeSock();
    SockUtil::setNoSigpipe(fd);
    SockUtil::setNoBlocked(fd);
    SockUtil::setCloExec(fd);
    return fromSock_l(std::make_shared<SockNum>(fd, type));
}

bool Socket::fromSock_l(SockNum::Ptr sock) {
    // attachEvent() 根据socket类型，向poller添加对应的事件监听, 并设置相应的回调
    if (!attachEvent(sock)) {
        return false;
    }
    setSock(std::move(sock));
    return true;
}

/*
    brief: 从另一个Socket复制， 让一个Socket被多个poller监听
    主要是创建主server时，让主server创建的socket同时被EventPollerPool中的所有poller监听
*/
bool Socket::cloneSocket(const Socket& other) {
    closeSock();
    SockNum::Ptr sock;
    {
        std::lock_guard<decltype(other.mtx_sock_fd_)> lock(other.mtx_sock_fd_);
        if (!other.sock_fd_) {
            WarnL << "sockfd of src socket is null";
            return false;
        }
        sock = other.sock_fd_->sockNum();
    }
    return fromSock_l(sock);
}

ssize_t Socket::send(const char* buf, size_t size, struct sockaddr* addr, 
                    socklen_t addr_len, bool try_flush) {
    if (size <= 0) {
        size = strlen(buf);
        if (!size) {
            return 0;
        }
    }
    auto ptr = BufferRaw::create();
    ptr->assign(buf, size);
    return send(std::move(ptr), addr, addr_len, try_flush);
}

ssize_t Socket::send(std::string buf, struct sockaddr* addr, socklen_t addr_len, bool try_flush) {
    return send(std::make_shared<BufferString>(std::move(buf)), addr, addr_len, try_flush);
}

ssize_t Socket::send(Buffer::Ptr buf, struct sockaddr* addr, socklen_t addr_len, bool try_flush) {
    if (!addr) {
        if (!udp_send_dst_) {
            return send_l(std::move(buf), false, try_flush);
        }
        addr = reinterpret_cast<struct sockaddr*>(udp_send_dst_.get());
        addr_len = SockUtil::getSockLen(addr);
    }
    return send_l(std::make_shared<BufferSock>(std::move(buf), addr, addr_len), true, try_flush);
}

ssize_t Socket::send_l(Buffer::Ptr buf, bool is_buf_sock, bool try_flush) {
    auto size = buf ? buf->size() : 0;
    if (!size) {
        return 0;
    }
    {
        // 将待发送内容加入到二级发送缓冲
        std::lock_guard<decltype(mtx_send_buf_waiting_)> lock(mtx_send_buf_waiting_);
        send_buf_waiting_.emplace_back(std::move(buf), is_buf_sock);
    }
    if (try_flush) {
        // 刷新缓冲区尝试发送数据
        if (flushAll()) {
            return -1;
        }
    }
    return size;
}

int Socket::flushAll() {
    std::lock_guard<decltype(mtx_sock_fd_)> lock(mtx_sock_fd_);
    if (!sock_fd_) {
        return -1;
    }
    if (sendable_) {
        return flushData(sock_fd_->sockNum(), false) ? 0 : -1;  // socket可写
    }
    // socket不可写，判断是否超时
    if (send_flush_ticker_.elapsedTime() > max_send_buffer_ms_) {
        emitErr(SockException(ErrorCode::Other, "socket send timeout"));
        return -1;
    }
    return 0;
}

void Socket::onFlushed() {
    bool flag;
    {
        std::lock_guard<decltype(mtx_event_)> lock(mtx_event_);
        flag = on_flush_();
    }
    if (!flag) {
        setOnFlush(nullptr);
    }
}

void Socket::setSock(SockNum::Ptr sock) {
    std::lock_guard<decltype(mtx_sock_fd_)> lock(mtx_sock_fd_);
    if (sock) {
        sock_fd_ = std::make_shared<SockFd>(sock, poller_);
        SockUtil::getSockLocalAddr(sock->rawFd(), local_addr_);
        SockUtil::getSockPeerAddr(sock->rawFd(), peer_addr_);
    } else {
        sock_fd_ = nullptr;
    }
}

void Socket::closeSock(bool close_fd) {
    // 重置Socket到初始状态
    sendable_ = true;
    enable_recv_ = true;
    enable_speed_ = false;
    con_timer_ = nullptr;
    async_con_cb_ = nullptr;
    send_flush_ticker_.resetTime();

    {
        std::lock_guard<decltype(mtx_send_buf_waiting_)> lock(mtx_send_buf_waiting_);
        send_buf_waiting_.clear();
    }

    {
        std::lock_guard<decltype(mtx_send_buf_sending_)> lock(mtx_send_buf_sending_);
        send_buf_sending_.clear();
    }

    {
        std::lock_guard<decltype(mtx_sock_fd_)> lock(mtx_sock_fd_);
        if (close_fd) {
            err_emit_ = false;
            sock_fd_ = nullptr;
        } else if (sock_fd_) {
            sock_fd_->delEvent();
        }
    }
}

size_t Socket::getSendBufferCount() {
    size_t ret = 0;
    {
        std::lock_guard<decltype(mtx_send_buf_waiting_)> lock(mtx_send_buf_waiting_);
        ret += send_buf_waiting_.size();
    }

    {
        std::lock_guard<decltype(mtx_send_buf_sending_)> lock(mtx_send_buf_sending_);
        send_buf_sending_.forEach([&](BufferList::Ptr& buf) {
            ret += buf->count();
        });
    }
    return ret;
}

uint64_t Socket::elapsedTimeAfterFlushed() {
    return send_flush_ticker_.elapsedTime();
}

int Socket::getRecvSpeed() {
    enable_speed_ = true;
    return recv_speed_.getSpeed();
}

int Socket::getSendSpeed() {
    enable_speed_ = true;
    return send_speed_.getSpeed();
}

std::string Socket::getLocalIp() {
    std::lock_guard<decltype(mtx_sock_fd_)> lock(mtx_sock_fd_);
    if (!sock_fd_) {
        return "";
    }
    return SockUtil::inetNtoa(reinterpret_cast<struct sockaddr*>(&local_addr_));
}

uint16_t Socket::getLocalPort() {
    std::lock_guard<decltype(mtx_sock_fd_)> lock(mtx_sock_fd_);
    if (!sock_fd_) {
        return 0;
    }
    return SockUtil::inetPort(reinterpret_cast<struct sockaddr*>(&local_addr_));
}

std::string Socket::getPeerIp() {
    std::lock_guard<decltype(mtx_sock_fd_)> lock(mtx_sock_fd_);
    if (!sock_fd_) {
        return "";
    }
    if (udp_send_dst_) {
        return SockUtil::inetNtoa(reinterpret_cast<struct sockaddr*>(udp_send_dst_.get()));
    }
    return SockUtil::inetNtoa(reinterpret_cast<sockaddr*>(&peer_addr_));
}

uint16_t Socket::getPeerPort() {
    std::lock_guard<decltype(mtx_sock_fd_)> lock(mtx_sock_fd_);
    if (!sock_fd_) {
        return 0;
    }
    if (udp_send_dst_) {
        return SockUtil::inetPort(reinterpret_cast<struct sockaddr*>(udp_send_dst_.get()));
    }
    return SockUtil::inetPort(reinterpret_cast<sockaddr*>(&peer_addr_));
}

std::string Socket::getIdentifier() const {
    static std::string class_name = "Socket: ";
    return class_name + std::to_string(reinterpret_cast<uint64_t>(this));
}

int Socket::onAccept(const SockNum::Ptr& sock, EventPoller::Poll_Event event) noexcept {
    int fd;
    struct sockaddr_storage peer_addr;
    socklen_t addr_len = sizeof peer_addr;
    while (true) {
        // 处理多个连接请求
        if (!(event & EventPoller::Poll_Event::Read_Event)) {
            do {
                // accept返回的fd用于实际的数据收发
                fd = accept(sock->rawFd(), reinterpret_cast<struct sockaddr*>(&peer_addr), &addr_len);
            } while ( -1 == fd && UV_EINTR == get_uv_error(true));
        
            // accept失败
            if (fd == -1) {
                int err = get_uv_error(true);
                if (err == UV_EAGAIN) {
                    return 0;  // 没有新连接, 直接return退出循环
                }
                auto ex = toSockException(err);
                ErrorL << "Accept socket failed: " << ex.what();
                // 可能打开的文件描述符太多了:UV_EMFILE/UV_ENFILE
                // 边缘触发，还需要手动再触发accept事件,
                std::weak_ptr<Socket> weak_self = shared_from_this();
                poller_->doDelayTask(100, [weak_self, sock]() {
                    if (auto strong_self = weak_self.lock()) {
                        strong_self->onAccept(sock, EventPoller::Poll_Event::Read_Event);
                    }
                    return 0;
                });
                return -1;
            }

            SockUtil::setNoSigpipe(fd);
            SockUtil::setNoBlocked(fd);
            SockUtil::setNoDelay(fd);
            SockUtil::setSendBuf(fd);
            SockUtil::setRecvBuf(fd);
            SockUtil::setCloseWait(fd);
            SockUtil::setCloExec(fd);

            Socket::Ptr peer_sock;
            try {
                std::lock_guard<decltype(mtx_event_)> lock(mtx_event_);
                // multi_poller_时从EventPollerPool中选择负载最小的poller，创建socket并使用选择的poller监听
                peer_sock = on_before_accept_(poller_);  // 非multi_poller_时，使用当前poller
            } catch (std::exception& ex) {
                ErrorL << "Exception occurred when emit on_before_accept: " << ex.what();
                close(fd);
                continue;
            }

            if (!peer_sock) {
                // 子Socket共用父Socket的poll线程并且关闭互斥锁
                peer_sock = Socket::createSocket(poller_, false);
            }

            auto sock = std::make_shared<SockNum>(fd, SockNum::SockType::TCP);  // 创建sock封装fd
            peer_sock->setSock(sock);  // 将fd关联到Socket实例
            memcpy(&peer_sock->peer_addr_, &peer_addr, addr_len);  // 设置对方的地址

            // shared_ptr<void>， 内容为nullptr带deleter的shared_ptr, 确保在异步操作完成后自动触发事件注册
            std::shared_ptr<void> completed(nullptr, [peer_sock, sock](void*) {
                try {
                    if (!peer_sock->attachEvent(sock)) {
                        peer_sock->emitErr(SockException(
                            ErrorCode::Eof,
                            "add event to poller failed when accept a socket"
                        ));
                    }
                } catch (std::exception& ex) {
                    ErrorL << "Exception occurred: " << ex.what();
                }
            });

            try {
                // 捕获异常，防止socket未accept尽，epoll边沿触发失效的问题
                std::lock_guard<decltype(mtx_event_)> lock(mtx_event_);
                on_accept_(peer_sock, completed);  // 连接回调, 创建连接的session，设置回调, 传递completed保证异步操作完成后自动触发事件注册
            } catch (std::exception& ex) {
                ErrorL << "Exception occured when emit on_accept: " << ex.what();
                continue;
            }
        }

        if (!(event & EventPoller::Poll_Event::Error_Event)) {
            auto ex = getSockErr(sock->rawFd());
            emitErr(ex);
            ErrorL << "TCP listener occurred a err: " << ex.what();
            return -1;
        }
    }
}

void Socket::onWriteAble(const SockNum::Ptr& sock) {
    bool empty_waiting;
    bool empty_sending;
    {
        std::lock_guard<decltype(mtx_send_buf_waiting_)> lock(mtx_send_buf_waiting_);
        empty_waiting = send_buf_waiting_.empty();
    }

    {
        std::lock_guard<decltype(mtx_send_buf_sending_)> lock(mtx_send_buf_sending_);
        empty_sending = send_buf_sending_.empty();
    }

    if (empty_waiting && empty_sending) {
        stopWriteAbleEvent(sock);  // 数据已经清空了，我们停止监听可写事件
    } else {
        flushData(sock, true);  // 尝试发送剩余的数据
    }
}

/*
    brief: 刷新缓冲区尝试发送数据
*/
bool Socket::flushData(const SockNum::Ptr& sock, bool poller_thread) {
    decltype(send_buf_sending_) send_buf_sending_tmp;
    {
        // 取出一级发送缓存的内容
        std::lock_guard<decltype(mtx_send_buf_sending_)> lock(mtx_send_buf_sending_);
        if (!send_buf_sending_.empty()) {
            send_buf_sending_tmp.swap(send_buf_sending_);
        }
    }

    // 一级发送缓存为空，则将二级发送缓存的内容加入到一级发送缓存
    if (send_buf_sending_tmp.empty()) {
        send_flush_ticker_.resetTime();
        do {
            {
                std::lock_guard<decltype(mtx_send_buf_waiting_)> lock(mtx_send_buf_waiting_);
                if (!send_buf_waiting_.empty()) {
                    std::lock_guard<decltype(mtx_event_)> lock(mtx_event_);
                    auto send_result = enable_speed_ ? [this](const Buffer::Ptr& buffer, bool send_success) {
                        if (send_success) {
                            send_speed_ += buffer->size();
                        }
                        std::lock_guard<decltype(mtx_event_)> lock(mtx_event_);
                        if (send_result_) {
                            send_result_(buffer, send_success);
                        }
                    } : send_result_;
                    // 将二级发送缓存的内容加入到一级发送缓存
                    // 将二级发送缓冲中的待发送的缓冲区封装到BufferList然后加入到一级发送缓冲
                    send_buf_sending_tmp.emplace_back(BufferList::create(
                        std::move(send_buf_waiting_), std::move(send_result),
                        sock->type() == SockNum::SockType::UDP
                    ));
                    break;
                }

                // 如果一级发送缓存也为空, 说明数据已全部写入socket
                if (poller_thread) {
                    stopWriteAbleEvent(sock);
                    onFlushed();
                }
                return true;
            }
        } while (false);
    }

    // 一级发送缓存不为空，则循环的从一级发送缓冲区中取出BufferList进行批量发送Buffer
    while (!send_buf_sending_tmp.empty()) {
        auto& packet = send_buf_sending_tmp.front();
        auto n = packet->send(sock->rawFd(), sock_flags_);
        InfoL << "send data size: " << n;
        if (n > 0) {
            // 全部发送成功
            if (packet->empty()) {
                send_buf_sending_tmp.pop_front();
                continue;
            }
            // 部分发送成功
            if (!poller_thread) {
                startWriteAbleEvent(sock);
            }
            break;
        }

        int err = get_uv_error(true);
        if (err == UV_EAGAIN) {
            // 等待下一次发送, 非阻塞时，发送缓冲区满返回EAGAIN
            if (!poller_thread) {
                startWriteAbleEvent(sock);
            }
            break;
        }

        // 其他错误代码，发生异常
        if (sock->type() == SockNum::SockType::UDP) {
            send_buf_sending_tmp.pop_front();
            WarnL << "Send udp socket[" << sock << "] failed, data ignored: " << uv_strerror(err);
            continue;
        }
        // tcp 发送失败时，触发异常
        emitErr(toSockException(err));
        return false;
    }

    // 数据未发送完的部分，放回到一级发送缓存
    if (!send_buf_sending_tmp.empty()) {
        std::lock_guard<decltype(mtx_send_buf_sending_)> lock(mtx_send_buf_sending_);
        send_buf_sending_tmp.swap(send_buf_sending_);
        send_buf_sending_.append(send_buf_sending_tmp);
        return true;
    }

    // 二级缓存已全部发送完毕，说明该socket还可写，我们尝试继续写
    // 如果是poller线程，我们尝试再次写一次(因为可能其他线程调用了send函数又有新数据了)
    return poller_thread ? flushData(sock, poller_thread) : true;
}

void Socket::startWriteAbleEvent(const SockNum::Ptr& sock) {
    sendable_ = false;
    EventPoller::Poll_Event flag = enable_recv_ ? EventPoller::Poll_Event::Read_Event 
                                                : EventPoller::Poll_Event::None_Event;
    poller_->modifyEvent(sock->rawFd(), 
                         flag | EventPoller::Poll_Event::Error_Event | EventPoller::Poll_Event::Write_Event,
                         [sock](bool) {});
}

void Socket::stopWriteAbleEvent(const SockNum::Ptr& sock) {
    sendable_ = true;
    EventPoller::Poll_Event flag = enable_recv_ ? EventPoller::Poll_Event::Read_Event 
                                                : EventPoller::Poll_Event::None_Event;
    poller_->modifyEvent(sock->rawFd(), flag | EventPoller::Poll_Event::Error_Event, [sock](bool) {});
}

void Socket::enableRecv(bool enabled) {
    if (enable_recv_ == enabled) {
        return ;
    }
    enable_recv_ = enabled;
    EventPoller::Poll_Event read_flag = enabled ? EventPoller::Poll_Event::Read_Event 
                                                : EventPoller::Poll_Event::None_Event;
    EventPoller::Poll_Event send_flag = sendable_ ? EventPoller::Poll_Event::None_Event 
                                                  : EventPoller::Poll_Event::Write_Event;  // 可写时，不监听可写事件
    poller_->modifyEvent(rawFd(), read_flag | send_flag | EventPoller::Poll_Event::Error_Event);
}

int Socket::rawFd() const {
    std::lock_guard<decltype(mtx_sock_fd_)> lock(mtx_sock_fd_);
    if (!sock_fd_) {
        return -1;
    }
    return sock_fd_->rawFd();
}

// alive状态，socket fd有效且没有触发错误回调
bool Socket::alive() const {
    std::lock_guard<decltype(mtx_sock_fd_)> lock(mtx_sock_fd_);
    return sock_fd_ && !err_emit_;
}

SockNum::SockType Socket::sockType() const {
    std::lock_guard<decltype(mtx_sock_fd_)> lock(mtx_sock_fd_);
    if (!sock_fd_) {
        return SockNum::SockType::Invalid;
    }
    return sock_fd_->type();
}

void Socket::setSendTimeOutSecond(uint32_t second) {
    max_send_buffer_ms_ = second * 1000;
}

bool Socket::isSocketBusy() const { return !sendable_.load(); }

const EventPoller::Ptr& Socket::getPoller() const { return poller_; }

bool Socket::bindPeerAddr(const struct sockaddr* dst_addr, socklen_t addr_len, bool soft_bind) {
    std::lock_guard<decltype(mtx_sock_fd_)> lock(mtx_sock_fd_);
    if (!sock_fd_) {
        return false;
    }
    if (sock_fd_->type() != SockNum::SockType::UDP) {
        return false;
    }
    addr_len = addr_len ? addr_len : SockUtil::getSockLen(dst_addr);
    if (soft_bind) {
        // 软绑定，只保存地址
        udp_send_dst_ = std::make_shared<struct sockaddr_storage>();
    } else {
        // 硬绑定后，取消软绑定，防止memcpy目标地址的性能损失
        udp_send_dst_ = nullptr;
        if (-1 == ::connect(sock_fd_->rawFd(), dst_addr, addr_len)) {
            WarnL << "Connect socket to peer address failed: " << SockUtil::inetNtoa(dst_addr);
            return false;
        }
        memcpy(&peer_addr_, dst_addr, addr_len);
    }
    return true;
}

void Socket::setSendFlags(int flags) { sock_flags_ = flags; };

///////////////////////////////// SockSender /////////////////////////////////


SockSender& SockSender::operator<<(Buffer::Ptr buf) {
    send(std::move(buf));
    return *this;
}

SockSender& SockSender::operator<<(std::string buf) {
    send(std::move(buf));
    return *this;
}

SockSender& SockSender::operator<<(const char* buf) {
    send(buf);
    return *this;
}

ssize_t SockSender::send(std::string buf) {
    return send(std::make_shared<BufferString>(std::move(buf)));
}

ssize_t SockSender::send(const char* buf, size_t size) {
    auto buffer = BufferRaw::create();
    buffer->assign(buf, size);
    return send(std::move(buffer));
}

///////////////////////////////// SocketHelper /////////////////////////////////


SocketHelper::SocketHelper(const Socket::Ptr& sock) {
    setSock(sock);
    setOnCreateSocket(nullptr);
}

const EventPoller::Ptr& SocketHelper::getPoller() const {
    assert(poller_);
    return poller_;
}

void SocketHelper::setSendFlushFlag(bool try_flush) { try_flush_ = try_flush; }

void SocketHelper::setSendFlags(int flags) {
    if (!sock_) {
        return ;
    }
    sock_->setSendFlags(flags);
}

bool SocketHelper::isSocketBusy() const {
    if (!sock_) {
        return false;
    }
    return sock_->isSocketBusy();
}

void SocketHelper::setOnCreateSocket(Socket::onCreateSocket cb) {
    if (cb) {
        on_create_socket_ = std::move(cb);
    } else {
        on_create_socket_ = [this](const EventPoller::Ptr& poller) {
            return Socket::createSocket(poller, false);
        };
    }
}

Socket::Ptr SocketHelper::createSocket() { return on_create_socket_(poller_); }

const Socket::Ptr& SocketHelper::getSock() const { return sock_; }

int SocketHelper::flushAll() {
    if (!sock_) {
        return -1;
    }
    return sock_->flushAll();
}

bool SocketHelper::overSsl() const { return false; }

std::string SocketHelper::getLocalIp() { return sock_ ? sock_->getLocalIp() : ""; }

uint16_t SocketHelper::getLocalPort() { return sock_ ? sock_->getLocalPort() : 0; }

std::string SocketHelper::getPeerIp() { return sock_ ? sock_->getPeerIp() : ""; }

uint16_t SocketHelper::getPeerPort() { return sock_ ? sock_->getPeerPort() : 0; }

Task::Ptr SocketHelper::async(TaskIn task, bool may_sync) {
    return poller_->async(std::move(task), may_sync);
}

Task::Ptr SocketHelper::asyncFirst(TaskIn task, bool may_sync) {
    return poller_->asyncFirst(std::move(task), may_sync);
}

ssize_t SocketHelper::send(Buffer::Ptr buf) {
    if (!sock_) {
        return -1;
    }
    return sock_->send(std::move(buf), nullptr, 0, try_flush_);
}

void SocketHelper::shutdown(const SockException& ex) {
    if (sock_) {
        sock_->emitErr(ex);
    }
}

void SocketHelper::safeShutdown(const SockException& ex) {
    std::weak_ptr<SocketHelper> weak_self = shared_from_this();
    asyncFirst([weak_self, ex]() {
        if (auto strong_self = weak_self.lock()) {
            strong_self->shutdown(ex);
        }
    });
}

void SocketHelper::setPoller(const EventPoller::Ptr& poller) { poller_ = poller; }

void SocketHelper::setSock(const Socket::Ptr& sock) { 
    sock_ = sock;
    if (sock_) {
        poller_ = sock_->getPoller();
    }
}

std::ostream& operator<<(std::ostream& os, const SockException& ex) {
    // os << ex.getErrCode() << "(" << ex.what() << ")";
    return os;
}

}  // namespace xkernel