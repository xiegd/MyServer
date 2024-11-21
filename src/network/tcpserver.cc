#include "tcpserver.h"

#include <exception>

namespace xkernel {

INSTANCE_IMP(SessionMap)  // ???
STATISTIC_IMPL(TcpServer)


TcpServer::TcpServer(EventPoller::Ptr& poller) : Server(poller){
    multi_poller_ = !poller;
    setOnCreateSocket(nullptr);
}

TcpServer::~TcpServer() {
    if (main_server_ && socket_ && socket_->rawFd() != -1) {
        InfoL  << "Close tcp server [" << socket_->getLocalIp()
               << "]: " << socket_->getLocalPort();
    }
    timer_.reset();
    socket_.reset();
    session_map_.clear();
    cloned_server_.clear();
}

uint16_t TcpServer::getPort() const {
    if (!socket_) {
        return 0;
    }
    return socket_->getLocalPort();
}

void TcpServer::setOnCreateSocket(Socket::onCreateSocket cb) {
    if (cb) {
        on_create_socket_ = std::move(cb);
    } else {
        on_create_socket_ = [this](const EventPoller::Ptr& poller) {
            return Socket::createSocket(poller, false);
        };
    }
    for (auto& pr : cloned_server_) {
        pr.second->setOnCreateSocket(cb);
    }
}

TcpServer::Ptr TcpServer::onCreateServer(const EventPoller::Ptr& poller) {
    return Ptr(new TcpServer(poller), [poller](TcpServer* ptr) {
        poller->async([ptr]() { delete ptr; });
    });
}

Session::Ptr TcpServer::onAcceptConnection(const Socket::Ptr& sock) {
    assert(poller_->isCurrentThread());
    std::weak_ptr<TcpServer> weak_self = std::static_pointer_cast<TcpServer>(shared_from_this());
    auto helper = session_alloc_(std::static_pointer_cast<TcpServer>(shared_from_this()), sock);
    auto session = helper->session();
    session->attachServer(*this);

    auto success = session_map_.emplace(helper.get(), helper).second;
    assert(success);

    std::weak_ptr<Session> weak_session = session;
    sock->setOnRead([weak_session](const Buffer::Ptr& buf, struct sockaddr*, int) {
        auto strong_session = weak_session.lock();
        if (!strong_session) {
            return ;
        }
        try {
            strong_session->onRecv(buf);
        } catch (SocketException& ex) {
            strong_session->shutdown(ex);
        } catch (std::exception& ex) {
            strong_session->shutdown(SocketException(ErrorCode::Shutdown, ex.what()));
        }
    });

    SessionHelper* ptr = helper.get();
    auto cls = ptr->className();

    sock->setOnErr([weak_self, weak_session, ptr, cls](const SockException& err) {
        onceToken token(nullptr, [&]() {
            auto strong_self = weak_self.lock();
            if (!strong_self) {
                return ;
            }

            assert(strong_self->poller_->isCurrentThread());
            if (!strong_self->is_on_manager_) {
                strong_self->session_map_.erase(ptr);
            } else {
                strong_self->poller_->async([weak_self, ptr]() {
                    auto strong_self = weak_self.lock();
                    if (strong_self) {
                        strong_self->session_map_.erase(ptr);
                    }
                }, false);
            }

        });

        auto strong_session = weak_session.lock();
        if (strong_session) {
            TraceP(strong_session) << cls << "on err: " << err;
            strong_session->onError(err);
        }
    });
    return session;
}

void TcpServer::start_l(uint16_t port, const std::string& host, uint32_t backlog) {

}

void TcpServer::cloneFrom(const TcpServer& that) {
    if (!that.socket_) {
        throw std::invalid_argument("TcpServer::cloneFrom other with null socket");
    }
    setupEvent();
    main_server_ = false;
    on_create_socket_ = that.on_create_socket;
    session_alloc_ = that.session_alloc;
    std::weak_ptr<TcpServer> weak_self = std::static_pointer_cast<TcpServer>(shared_from_this());
    timer_ = std::make_shared<Timer>(2.0f, [weak_self]() -> bool {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return false;
        }
        strong_self->onManagerSession();
        return true;
    }, poller_);
    this->mIni::operator=(that);  // ???
    parent_ = static_pointer_cast<TcpServer>(const_cast<TcpServer&>(that).shared_from_this());
}

Socket::Ptr TcpServer::onBeforeAcceptConection(const EventPoller::Ptr& poller) {
    assert(poller_->isCurrentThread());
    return createSocket(multi_poller_ ? EventPollerPool::Instance().getPoller(false) : poller_);
}

void TcpServer::onManagerSession() {
    assert(poller_->isCurrentThread());
    onceToken token([&]() { is_on_manager_ = true; }, [&]() { is_on_manager_ = false; });
    for (auto& pr : session_map_) {
        try {
            pr.second->session()->onManager();  // onManager 没有实现???
        } catch (std::exception& ex) {
            WarnL << ex.what();
        }
    }
}

Session::Ptr TcpServer::createSession(const Socket::Ptr& socket) {
    return getServer(socket->getPoller().get())->onAcceptConnection(sock);
}

Socket::Ptr TcpServer::createSocket(const EventPoller::Ptr& poller) {
    return on_create_socket_(poller);
}

TcpServer::Ptr TcpServer::getServer(const EventPoller* poller) const {
    auto parent = parent_.lock();
    auto& ref = parent ? parent->cloned_server_ : cloned_server_;
    auto it = ref.find(poller);
    if (it != ref.end()) {
        return it->second;
    }

    return std::static_pointer_cast<TcpServer>(
        parent ? parent : const_cast<TcpServer*>(this)->shared_from_this());
}

void TcpServer::setupEvent() {
    socket_ = createSocket(poller_);
    std::weak_ptr<TcpServer> weak_self = std::static_pointer_cast<TcpServer>(shared_from_this());
    socket_->setOnBeforeAccept([weak_self](const EventPoller::Ptr& poller) -> Socket::Ptr {
        if (auto strong_self = weak_self.lock()) {
            return strong_self->onBeforeAcceptConnection(poller);
        }
        return nullptr;
    });

    socket_->setOnAccept([weak_self](Socket::Ptr& sock, std::shared_ptr<void>& complete) {
        if (auto strong_self = weak_self.lock()) {
            auto ptr = sock->getPoller().get();
            auto server = strong_self->getServer(ptr);
            ptr->async([server, sock, complete]() {
                server->onAcceptConnection(sock);
            });
        }
    });
}

}  // namespace xkernel