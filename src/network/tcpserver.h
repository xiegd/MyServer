#ifndef _TCPSERVER_H_
#define _TCPSERVER_H_

#include <functional>
#include <memory>
#include <unordered_map>

#include "server.h"
#include "session.h"
#include "utility.h"
#include "timer.h"
#include "socket.h"

namespace xkernel {

class TcpServer : public Server {
public:
    using Ptr = std::shared_ptr<TcpServer>;

    explicit TcpServer(const EventPoller::Ptr& poller = nullptr);
    ~TcpServer() override;

public:
    template <typename SessionType>
    void start(uint16_t port, const std::string& host = "::", uint32_t backlog = 1024,
               const std::function<void(std::shared_ptr<SessionType>&)>& cb = nullptr) {
        static std::string cls_name = xkernel::demangle(typeid(SessionType).name());
        // 设置会话创建器, 实现创建不同类型的服务器
        session_alloc_ = [cb] (const TcpServer::Ptr& server, const Socket::Ptr& sock) -> SessionHelper::Ptr {
            auto session = std::shared_ptr<SessionType>(
                new SessionType(sock), [](SessionType* ptr) {
                    TraceP(static_cast<Session*>(ptr)) << "~" << cls_name;
                    delete ptr;
                });
            if (cb) {
                cb(session);
            }
            InfoL << session.get();
            TraceP(static_cast<Session*>(session.get())) << cls_name;
            session->setOnCreateSocket(server->on_create_socket_);
            InfoL << "session->setOnCreateSocket(server->on_create_socket_)";
            return std::make_shared<SessionHelper>(server, std::move(session), cls_name);
        };
        start_l(port, host, backlog);
    }

    uint16_t getPort() const;
    void setOnCreateSocket(Socket::onCreateSocket cb);
    Session::Ptr createSession(const Socket::Ptr& socket);

protected:
    virtual void cloneFrom(const TcpServer& that);
    virtual TcpServer::Ptr onCreateServer(const EventPoller::Ptr& poller);
    virtual Session::Ptr onAcceptConnection(const Socket::Ptr& sock);
    virtual Socket::Ptr onBeforeAcceptConnection(const EventPoller::Ptr& poller);

private:
    void onManagerSession();
    Socket::Ptr createSocket(const EventPoller::Ptr& poller);
    void start_l(uint16_t port, const std::string& host, uint32_t backlog);
    Ptr getServer(const EventPoller* poller) const;
    void setupEvent();

private:
    bool multi_poller_;
    bool is_on_manager_ = false;
    bool main_server_ = true;
    std::weak_ptr<TcpServer> parent_;
    Socket::Ptr socket_;  // 服务器的监听socket
    std::shared_ptr<Timer> timer_;
    Socket::onCreateSocket on_create_socket_;
    std::unordered_map<SessionHelper*, SessionHelper::Ptr> session_map_;  // 当前的所有连接对应的session
    std::function<SessionHelper::Ptr(const TcpServer::Ptr&, const Socket::Ptr&)> session_alloc_;
    std::unordered_map<const EventPoller*, Ptr> cloned_server_;
    ObjectCounter<TcpServer> counter_;
};

}  // namespace xkernel

#endif