#ifndef _TCPCLIENT_H_
#define _TCPCLIENT_H_

#include <memory>

#include "socket.h"
#include "SSLbox.h"
#include "sockutil.h"

namespace xkernel {

class TcpClient : public SocketHelper {
public:
    using Ptr = std::shared_ptr<TcpClient>;

    TcpClient(const EventPoller::Ptr& poller = nullptr);
    ~TcpClient() override;

public:
    virtual void startConnect(const std::string& url, uint16_t port, 
                              float timeout_sec= 5, uint16_t local_port = 0);
    virtual void startConnectWithProxy(const std::string& url, const std::string& proxy_host,
                                       uint16_t proxy_port, float timeout_sec = 5,
                                       uint16_t local_port = 0);
    void shutdown(const SockException& ex = SockException(ErrorCode::Shutdown, "self shutdown")) override;
    virtual bool alive() const;
    virtual void setNetAdapter(const std::string& local_ip);
    std::string getIdentifier() const override;

protected:
    virtual void onConnect(const SockException& ex) = 0;
    void onManager() override;

private:
    void onSockConnect(const SockException& ex);

private:
    mutable std::string id_;
    std::string net_adapter_ = "::";
    std::shared_ptr<Timer> timer_;
    ObjectCounter<TcpClient> counter_;
};

template <typename TcpClientType>
class TcpClientWithSSL : public TcpClientType {
public:
    using Ptr = std::shared_ptr<TcpClientWithSSL>;

    template <typename... ArgsType>
    TcpClientWithSSL(ArgsType&&... args) : TcpClientType(std::forward<ArgsType>(args)...) {}

    ~TcpClientWithSSL() override {
        if (ssl_box_) {
            ssl_box_->flush();
        }
    }

public:
    void onRecv(const Buffer::Ptr& buf) override {
        if (ssl_box_) {
            ssl_box_->onRecv(buf);
        } else {
            TcpClientType::onRecv(buf);
        }
    }

    using TcpClientType::send;  // 名称继承

    ssize_t send(Buffer::Ptr buf) override {
        if (ssl_box_) {
            auto size = buf->size();
            ssl_box_->onSend(std::move(buf));
            return size;
        }
        return TcpClientType::send(std::move(buf));
    }

    void startConnect(const std::string& url, uint16_t port, 
                      float timeout_sec = 5, uint16_t local_port = 0) override {
        host_ = url;
        TcpClientType::startConnect(url, port, timeout_sec, local_port);
    }

    void startConnectWithProxy(const std::string& url, const std::string& proxy_host,
                               uint16_t proxy_port, float timeout_sec = 5, 
                               uint16_t local_port = 0) override {
        host_ = url;
        TcpClientType::startConnect(proxy_host, proxy_port, timeout_sec, local_port);
    }

    bool overSsl() const override { return (bool)ssl_box_; }

protected:
    void onConnect(const SockException& ex) override {
        if (!ex) {
            ssl_box_ = std::make_shared<SSLBox>(false);
            ssl_box_->setOnDecData([this](const Buffer::Ptr& buf) {
                TcpClientType::onRecv(buf);
            });
            ssl_box_->setOnEncData([this](const Buffer::Ptr& buf) {
                TcpClientType::send(buf);
            });

            if (!SockUtil::isIP(host_.data())) {
                ssl_box_->setHost(host_.data());
            }
        }
        TcpClientType::onConnect(ex);
    }

    void setDoNotUseSSL() { ssl_box_.reset(); }

private:
    std::string host_;
    std::shared_ptr<SSLBox> ssl_box_;
};

}  // namespace xkernel

#endif