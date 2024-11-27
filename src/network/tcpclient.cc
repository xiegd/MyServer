#include "tcpclient.h"

#include <string>

namespace xkernel {

STATISTIC_IMPL(TcpClient);

TcpClient::TcpClient(const EventPoller::Ptr& poller ) : SocketHelper(nullptr) {
    setPoller(poller ? poller : EventPollerPool::Instance().getPoller());
    setOnCreateSocket([](const EventPoller::Ptr& poller) {
        return Socket::createSocket(poller, true);
    });
}

TcpClient::~TcpClient() { TraceL << "~" << TcpClient::getIdentifier(); }

void TcpClient::startConnect(const std::string& url, uint16_t port, 
                              float timeout_sec, uint16_t local_port) {
    std::weak_ptr<TcpClient> weak_self = 
        std::static_pointer_cast<TcpClient>(shared_from_this());
    timer_ = std::make_shared<Timer>(2.0f, [weak_self]() {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return false;
        }
        strong_self->onManager();
        return true;
    }, getPoller());

    setSock(createSocket());

    auto sock_ptr = getSock().get();
    sock_ptr->setOnErr([weak_self, sock_ptr](const SockException& ex) {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return ;
        }
        if (sock_ptr != strong_self->getSock().get()) {
            return ;
        }
        strong_self->timer_.reset();
        TraceL << strong_self->getIdentifier() << " on err: " << ex;
        strong_self->onErr(ex);
    });

    TraceL << getIdentifier() << " start connect " << url << ":" << port;
    sock_ptr->connect(url, port, [weak_self](const SockException& err) {
        auto strong_self = weak_self.lock();
        if (strong_self) {
            strong_self->onSockConnect(err);
        }
    }, timeout_sec, net_adapter_, local_port);
}

void TcpClient::onSockConnect(const SockException& ex) {
    TraceL << getIdentifier() << " connect result: " << ex;
    if (ex) {
        timer_.reset();
        onConnect(ex);
        return ;
    }

    auto sock_ptr = getSock().get();
    std::weak_ptr<TcpClient> weak_self = std::static_pointer_cast<TcpClient>(shared_from_this());
    sock_ptr->setOnFlush([weak_self, sock_ptr]() {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return false;
        }
        if (sock_ptr != strong_self->getSock().get()) {
            return false;
        }
        strong_self->onFlush();
        return true;
    });

    sock_ptr->setOnRead([weak_self, sock_ptr](const Buffer::Ptr& pBuf, struct sockaddr*, int) {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return ;
        }
        if (sock_ptr != strong_self->getSock().get()) {
            return ;
        }
        try {
            strong_self->onRecv(pBuf);
        } catch (std::exception& ex) {
            strong_self->shutdown(SockException(ErrorCode::Other, ex.what()));
        }
    });

    onConnect(ex);
}

void TcpClient::startConnectWithProxy(const std::string& url, const std::string& proxy_host,
                                       uint16_t proxy_port, float timeout_sec,
                                       uint16_t local_port) {}

void TcpClient::shutdown(const SockException& ex) {
    timer_.reset();
    SocketHelper::shutdown(ex);
}

bool TcpClient::alive() const {
    if (timer_) {
        return true;
    }
    auto sock = getSock();
    return sock && sock->alive();
}

void TcpClient::setNetAdapter(const std::string& local_ip) {
    net_adapter_ = local_ip;
}

std::string TcpClient::getIdentifier() const {
    if (id_.empty()) {
        static std::atomic<uint64_t> s_index{0};
        id_ = xkernel::demangle(typeid(*this).name())+ "-" + std::to_string(++s_index);
    }
    return id_;
}

void TcpClient::onManager() {}

}