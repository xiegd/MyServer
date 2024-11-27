#include "udpserver.h"
#include "utility.h"
#include "eventpoller.h"
#include "logger.h"
#include "uv_errno.h"

namespace xkernel {

STATISTIC_IMPL(UdpServer)

// ipv4映射到ipv6的固定前缀
static const uint8_t s_in6_addr_maped[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                           0x00, 0x00, 0x00, 0x00, 0xff, 0xff,
                                           0x00, 0x00, 0x00, 0x00};

static constexpr auto kUdpDelayCloseMs = 3 * 1000;

static UdpServer::PeerIdType makeSockId(struct sockaddr* addr, int addr_len) {
    UdpServer::PeerIdType ret;
    ret.resize(18);
    switch (addr->sa_family) {
        case AF_INET: {
            ret[0] = reinterpret_cast<sockaddr_in*>(addr)->sin_port >> 8;
            ret[1] = reinterpret_cast<sockaddr_in*>(addr)->sin_port & 0xFF;
            memcpy(&ret[2], &s_in6_addr_maped, 12);  // 填充前缀
            memcpy(&ret[14], &reinterpret_cast<sockaddr_in*>(addr)->sin_addr, 4);  // 填充ip
            return ret;
        }
        case AF_INET6: {
            ret[0] = reinterpret_cast<sockaddr_in6*>(addr)->sin6_port >> 8;
            ret[1] = reinterpret_cast<sockaddr_in6*>(addr)->sin6_port & 0xFF;
            memcpy(&ret[2], &reinterpret_cast<sockaddr_in6*>(addr)->sin6_addr, 16);
            return ret;
        }
        default:
            assert(0);
            return "";
    }
}

UdpServer::UdpServer(const EventPoller::Ptr& poller) {
    multi_poller_ = !poller;
    setOnCreateSocket(nullptr);
}

UdpServer::~UdpServer() {
    if (!cloned_ && socket_ && socket_->rawFd() != -1) {
        InfoL << "Close udp server [" << socket_->getLocalIp()
              << "]: " << socket_->getLocalPort();
    }
    timer_.reset();
    socket_.reset();
    cloned_server_.clear();
    if (!cloned_ && session_mutex_ && session_map_) {
        std::lock_guard<decltype(*session_mutex_)> lock(*session_mutex_);
        session_map_->clear();
    }
}

uint16_t UdpServer::getPort() {
    if (!socket_) {
        return 0;
    }
    return socket_->getLocalPort();
}

void UdpServer::setOnCreateSocket(onCreateSocket cb) {
    if (cb) {
        on_create_socket_ = std::move(cb);
    } else {
        on_create_socket_ = [](const EventPoller::Ptr& poller, const Buffer::Ptr& buf,
                               struct sockaddr* addr, int addr_len) {
            return Socket::createSocket(poller, false);
        };
    }
    for (auto& pr : cloned_server_) {
        pr.second->setOnCreateSocket(cb);
    }
}

UdpServer::Ptr UdpServer::onCreateServer(const EventPoller::Ptr& poller) {
    return Ptr(new UdpServer(poller), [poller](UdpServer* ptr) {
        poller->async([ptr]() { delete ptr; });
    });
}

void UdpServer::cloneFrom(const UdpServer& that) {
    if (!that.socket_) {
        throw std::invalid_argument("UdpServer::cloneFrom other with null socket");
    }
    setupEvent();
    cloned_ = true;
    on_create_socket_ = that.on_create_socket_;
    session_alloc_ = that.session_alloc_;
    session_mutex_ = that.session_mutex_;
    session_map_ = that.session_map_;
    this->mIni::operator=(that);  // 复制配置
}

void UdpServer::start_l(uint16_t port, const std::string& host) {
    setupEvent();
    // 主server才创建session map，其他cloned server共享
    session_mutex_ = std::make_shared<std::recursive_mutex>();
    session_map_ = std::make_shared<std::unordered_map<PeerIdType, SessionHelper::Ptr>>();

    std::weak_ptr<UdpServer> weak_self =
        std::static_pointer_cast<UdpServer>(shared_from_this());
    timer_ = std::make_shared<Timer>(
        2.0f,
        [weak_self]() -> bool {
            if (auto strong_self = weak_self.lock()) {
                strong_self->onManagerSession();
                return true;
            }
            return false;
        }, poller_);

    if (multi_poller_) {
        // 克隆server至不同线程，让udp server支持多线程
        EventPollerPool::Instance().forEach([&](const TaskExecutor::Ptr& excutor) {
            auto poller = std::static_pointer_cast<EventPoller>(excutor);
            if (poller == poller_) {
                return ;
            }
            auto& serverRef = cloned_server_[poller.get()];
            if (!serverRef) {
                serverRef = onCreateServer(poller);
            }
            if (serverRef) {
                serverRef->cloneFrom(*this);
            }
        });
    }

    if (!socket_->bindUdpSock(port, host.c_str())) {
        std::string err = (StrPrinter << "Bind udp socket on" << host << " "
                                      << port << " failed: " << get_uv_errmsg(true));
        throw std::runtime_error(err);
    }

    for (auto& pr : cloned_server_) {
        pr.second->socket_->cloneSocket(*socket_);
    }
    InfoL << "UDP server bind to [" << host << "]: " << port;
}

void UdpServer::onManagerSession() {
    decltype(session_map_) copy_map;
    {
        std::lock_guard<decltype(*session_mutex_)> lock(*session_mutex_);
        copy_map = std::make_shared<
            std::unordered_map<PeerIdType, SessionHelper::Ptr>>(*session_map_);  // 防止遍历时移除
    }
    auto lam = [copy_map]() {
        for (auto& pr : *copy_map) {
            auto& session = pr.second->session();
            if (!session->getPoller()->isCurrentThread()) {
                continue;
            }
            try {
                session->onManager();
            } catch (std::exception& ex) {
                WarnL << "Exception occurred when emit onManager: " << ex.what();
            }
        }
    };

    if (multi_poller_) {
        EventPollerPool::Instance().forEach([lam](const TaskExecutor::Ptr& executor) {
            std::static_pointer_cast<EventPoller>(executor)->async(lam);
        });
    } else {
        lam();
    }
}

void UdpServer::onRead(Buffer::Ptr& buf, struct sockaddr* addr, int addr_len) {
    const auto id = makeSockId(addr, addr_len);
    onRead_l(true, id, buf, addr, addr_len);
}

static void emitSessionRecv(const SessionHelper::Ptr& helper, const Buffer::Ptr& buf) {
    if (!helper->enable) {
        return ;
    }
    try {
        helper->session()->onRecv(buf);
    } catch (SockException& ex) {
        helper->session()->shutdown(ex);
    } catch (std::exception& ex) {
        helper->session()->shutdown(SockException(ErrorCode::Shutdown, ex.what()));
    }
}

void UdpServer::onRead_l(bool is_server_fd, const PeerIdType& id, Buffer::Ptr& buf,
              struct sockaddr* addr, int addr_len) {
    bool is_new = false;
    if (auto helper = getOrCreateSession(id, buf, addr, addr_len, is_new)) {
        emitSessionRecv(helper, buf);  // 当前线程收到数据，直接处理
    } else {
        WarnL << "UDP packet incoming from other thread";
        std::weak_ptr<SessionHelper> weak_helper = helper;
        auto cacheable_buf = std::move(buf);
        helper->session()->async([weak_helper, cacheable_buf]() {
            if (auto strong_helper = weak_helper.lock()) {
                emitSessionRecv(strong_helper, cacheable_buf);
            }
        });

#if !defined(NDEBUG)
        if (is_new) {
            TraceL << "UDP packet incoming from " << (is_server_fd ? "server fd" : "other peer fd");
        }
#endif
    }
}

SessionHelper::Ptr UdpServer::getOrCreateSession(
    const PeerIdType& id, Buffer::Ptr& buf, struct sockaddr* addr, int addr_len, bool& is_new) {
    {
        std::lock_guard<decltype(*session_mutex_)> lock(*session_mutex_);
        auto it = session_map_->find(id);
        if (it != session_map_->end()) {
            return it->second;
        }
    }
    is_new = true;
    return createSession(id, buf, addr, addr_len);
}

SessionHelper::Ptr UdpServer::createSession(
    const PeerIdType& id, Buffer::Ptr& buf, struct sockaddr* addr, int addr_len) {
    auto socket = createSocket(
        multi_poller_ ? EventPollerPool::Instance().getPoller(false) : poller_,
        buf, addr, addr_len);
    if (!socket) {
        return nullptr;
    }

    auto addr_str = std::string(reinterpret_cast<char*>(addr), addr_len);
    std::weak_ptr<UdpServer> weak_self = std::static_pointer_cast<UdpServer>(shared_from_this());
    auto helper_creator = [this, weak_self, socket, addr_str, id]() -> SessionHelper::Ptr {
        auto server = weak_self.lock();
        if (!server) {
            return nullptr;
        }

        std::lock_guard<decltype(*session_mutex_)> lock(*session_mutex_);
        // 如果已经创建该客户端对应的UdpSession类，那么直接返回
        auto it = session_map_->find(id);
        if (it != session_map_->end()) {
            return it->second;
        }

        assert(server->socket_);
        socket->bindUdpSock(server->socket_->getLocalPort(), socket_->getLocalIp());
        socket->bindPeerAddr(reinterpret_cast<const struct sockaddr*>(addr_str.data()), addr_str.size());
        auto helper = session_alloc_(server, socket);
        helper->session()->attachServer(*this);  // 把本服务器的配置传递给 Session

        std::weak_ptr<SessionHelper> weak_helper = helper;
        socket->setOnRead([weak_self, weak_helper, id](
            Buffer::Ptr& buf, struct sockaddr* addr, int addr_len) {
                auto strong_self = weak_self.lock();
                if (!strong_self) {
                    return ;
                }

                if (id == makeSockId(addr, addr_len)) {
                   if (auto strong_helper = weak_helper.lock()) {
                        emitSessionRecv(strong_helper, buf);
                   }
                   return ;
                }

                strong_self->onRead_l(false, id, buf, addr, addr_len);
        });

        socket->setOnErr([weak_self, weak_helper, id](const SockException& err) {
            onceToken token(nullptr, [&]() {
                auto strong_self = weak_self.lock();
                if (!strong_self) {
                    return ;
                }
                // 延时关闭会话，防止频繁快速重建对象
                strong_self->poller_->doDelayTask(kUdpDelayCloseMs, [weak_self, id]() {
                    if (auto strong_self = weak_self.lock()) {
                        std::lock_guard<decltype(*strong_self->session_mutex_)>
                            lock(*strong_self->session_mutex_);
                        strong_self->session_map_->erase(id);
                    }
                    return 0;
                });
            });

            if (auto strong_helper = weak_helper.lock()) {
                TraceP(strong_helper->session()) << strong_helper->className() << "on error: " << err;
                strong_helper->enable = false;
                strong_helper->session()->onErr(err);
            }
        });
        auto pr = session_map_->emplace(id, std::move(helper));
        assert(pr.second);
        return pr.first->second;
    };

    if (socket->getPoller()->isCurrentThread()) {
        return helper_creator();
    }

    auto cacheable_buf = std::move(buf);
    socket->getPoller()->async([helper_creator, cacheable_buf]() {
        auto helper = helper_creator();
        if (helper) {
            helper->session()->getPoller()->async([helper, cacheable_buf]() {
                emitSessionRecv(helper, cacheable_buf);
            });
        }
    });
    return nullptr;
}

Socket::Ptr UdpServer::createSocket(
    const EventPoller::Ptr& poller, const Buffer::Ptr& buf, struct sockaddr* addr, int addr_len) {
    return on_create_socket_(poller, buf, addr, addr_len);
}
void UdpServer::setupEvent() {
    socket_ = createSocket(poller_);
    std::weak_ptr<UdpServer> weak_self = std::static_pointer_cast<UdpServer>(shared_from_this());
    socket_->setOnRead([weak_self](Buffer::Ptr& buf, struct sockaddr* addr, int addr_len) {
        if (auto strong_self = weak_self.lock()) {
            strong_self->onRead(buf, addr, addr_len);
        }
    });
}

}  // namespace xkernel