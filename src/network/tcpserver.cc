#include "tcpserver.h"

#include <exception>

#include "uv_errno.h"

namespace xkernel {

// INSTANCE_IMP(SessionMap)  // ???
STATISTIC_IMPL(TcpServer)


TcpServer::TcpServer(const EventPoller::Ptr& poller) : Server(poller) {
    multi_poller_ = !poller;  // 默认 poller = nullptr
    // 这里使用成员初始化列表传入一个默认的nullptr来初始化Server::poller_
    setOnCreateSocket(nullptr);  // 只有主server才调用构造函数
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

/*
    brief: 设置创建socket的回调函数, 
    param:
        cb: 创建socket的回调函数, 主server的cb为空
*/
void TcpServer::setOnCreateSocket(Socket::onCreateSocket cb) {
    if (cb) {
        on_create_socket_ = std::move(cb);
    } else {
        on_create_socket_ = [this](const EventPoller::Ptr& poller) {
            return Socket::createSocket(poller, false);
        };
    }
    for (auto& pr : cloned_server_) {
        // 主server调用时，cloned_server_为空，不会进行填充，只会填充主server的on_create_socket_
        pr.second->setOnCreateSocket(cb);
    }
}

/*
    brief: 创建TcpServer子server
*/
TcpServer::Ptr TcpServer::onCreateServer(const EventPoller::Ptr& poller) {
    return Ptr(new TcpServer(poller), [poller](TcpServer* ptr) {
        poller->async([ptr]() { delete ptr; });
    });
}

/*
    brief: 接受新连接
    param:
        sock: 新连接的socket
*/
Session::Ptr TcpServer::onAcceptConnection(const Socket::Ptr& sock) {
    assert(poller_->isCurrentThread());
    std::weak_ptr<TcpServer> weak_self = std::static_pointer_cast<TcpServer>(shared_from_this());
    InfoL << "onAcceptConnection, before create session";
    // 调用session_alloc_出现错误，
    auto helper = session_alloc_(std::static_pointer_cast<TcpServer>(shared_from_this()), sock);
    InfoL << "helper->session(), create session";
    auto session = helper->session();
    session->attachServer(*this);

    auto success = session_map_.emplace(helper.get(), helper).second;
    assert(success);

    std::weak_ptr<Session> weak_session = session;
    // 设置socket::on_multi_read_
    sock->setOnRead([weak_session](const Buffer::Ptr& buf, struct sockaddr*, int) {
        auto strong_session = weak_session.lock();
        if (!strong_session) {
            return ;
        }
        try {
            strong_session->onRecv(buf);  // 建立了tcp连接后，如果发生可读事件则会调用
        } catch (SockException& ex) {
            strong_session->shutdown(ex);
        } catch (std::exception& ex) {
            strong_session->shutdown(SockException(ErrorCode::Shutdown, ex.what()));
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
            TraceP(strong_session) << cls << " on err: " << err;
            strong_session->onErr(err);
        }
    });
    return session;
}

/*
    brief: 实际启动服务器
    param:
        port: 端口号
        host: 主机地址
        backlog: 连接队列大小
*/
void TcpServer::start_l(uint16_t port, const std::string& host, uint32_t backlog) {
    setupEvent();  // 设置socket_，以及socket_的on_before_accept_和on_accept_事件回调

    std::weak_ptr<TcpServer> weak_self = std::static_pointer_cast<TcpServer>(shared_from_this());
    // 设置定时器，定时管理session
    timer_ = std::make_shared<Timer>(2.0f, [weak_self]() -> bool {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return false;
        }
        strong_self->onManagerSession();
        return true;
    }, poller_);

    if (multi_poller_) {
        EventPollerPool::Instance().forEach([&](const TaskExecutor::Ptr& executor) {
            EventPoller::Ptr poller = std::static_pointer_cast<EventPoller>(executor);
            if (poller == poller_) {
                return ;
            }
            // clone_server_原为空，这里使用poller作为key，进行访问，若不存在则会进行创建相应的键值对
            // 但是对应的value还是为空，所以需要调用onCreateServer()进行创建, 同时将其与线程池的poller进行绑定
            // 所以是在这里创建子server并填充cloned_server_的
            auto& server_ref = cloned_server_[poller.get()];
            if (!server_ref) {
                server_ref = onCreateServer(poller);  // 防止可能出现的poller为空的情况
            }
            if (server_ref) {
                server_ref->cloneFrom(*this);
            }
        });
    }
    // 创建监听socket
    if (!socket_->listen(port, host.c_str(), backlog)) {
        std::string err = (StrPrinter << "Listen on " << host << " " << port
                                      << " failed: " << get_uv_errmsg(true));
        throw std::runtime_error(err);
    }
    // 让所有server使用同一个socket，同时对所有EventPoller设置事件监听和回调
    for (auto& pr : cloned_server_) {
        pr.second->socket_->cloneSocket(*socket_);
    }
    InfoL << "TCP server listening on [" << host << "]: " << port;
}

/*
    brief: 复制主server的配置
*/
void TcpServer::cloneFrom(const TcpServer& that) {
    if (!that.socket_) {
        throw std::invalid_argument("TcpServer::cloneFrom other with null socket");
    }
    setupEvent();
    main_server_ = false;
    on_create_socket_ = that.on_create_socket_;
    session_alloc_ = that.session_alloc_;
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
    parent_ = std::static_pointer_cast<TcpServer>(const_cast<TcpServer&>(that).shared_from_this());  // 配置主server
}

/*
    brief: 对应socket::on_before_accept_, 用于在Socket::onAccept()中创建socket
    socket在接收到accept后，首先选择负载最小的poller，然后创建socket并使用这个poller监听
*/
Socket::Ptr TcpServer::onBeforeAcceptConnection(const EventPoller::Ptr& poller) {
    assert(poller_->isCurrentThread());
    return createSocket(multi_poller_ ? EventPollerPool::Instance().getPoller(false) : poller_);
}

/*
    brief: 定时管理session
*/
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
    return getServer(socket->getPoller().get())->onAcceptConnection(socket);
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

/*
    brief: 创建socket(选择poller监听socket)，并设置socket的事件回调
*/
void TcpServer::setupEvent() {
    // poller_继承自Server, 是EventPoller::Ptr类型, 对于主server默认为nullptr
    socket_ = createSocket(poller_);  // 从EventPollerPool中选择负载最小的poller，创建一个socket并使用这个poller监听 
    std::weak_ptr<TcpServer> weak_self = std::static_pointer_cast<TcpServer>(shared_from_this());
    // 设置before accept事件回调, 在accept前调用，创建socket
    socket_->setOnBeforeAccept([weak_self](const EventPoller::Ptr& poller) -> Socket::Ptr {
        if (auto strong_self = weak_self.lock()) {
            return strong_self->onBeforeAcceptConnection(poller);
        }
        return nullptr;
    });
    // 设置accept事件回调, 将新连接分发到对应的线程处理, 这个回调对应socket::on_accept_
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