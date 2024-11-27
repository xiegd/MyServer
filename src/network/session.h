#ifndef _SESSION_H_
#define _SESSION_H_

#include <memory>

#include "Socket.h"
#include "SSLbox.h"
#include "utility.h"

namespace xkernel {

class Server;
class TcpSession;
class UdpSession;

// session继承了SocketHelper的接口但是没有实现，所以需要其子类实现
class Session : public SocketHelper {
public:
    using Ptr = std::shared_ptr<Session>;

    Session(const Socket::Ptr& sock);
    ~Session() override = default;

public:
    virtual void attachServer(const Server& server) {}
    std::string getIdentifier() const override;

private:
    mutable std::string id_;
    std::unique_ptr<ObjectCounter<TcpSession>> tcp_counter_;
    std::unique_ptr<ObjectCounter<UdpSession>> udp_counter_;
};

template <typename SessionType>
class SessionWithSSL : public SessionType {
public:
    template <typename... ArgsType>
    SessionWithSSL(ArgsType&&... args) : SessionType(std::forward<ArgsType>(args)...) {
        ssl_box_.setOnEncData([&](const Buffer::Ptr& buf) { SessionType::send(buf); });
        ssl_box_.setOnDecData([&](const Buffer::Ptr& buf) { SessionType::onRecv(buf); });
    }

    ~SessionWithSSL() override { ssl_box_.flush(); }
    bool overSsl() const override { return true; }

protected:
    ssize_t send(Buffer::Ptr buf) override {
        auto size = buf->size();
        ssl_box_.onSend(std::move(buf));
        return size;
    }

private:
    SSLBox ssl_box_;
};

}  // namespace xkernel

#endif