#ifndef _UDPSERVER_H_
#define _UDPSERVER_H_

#include "server.h"
#include "session.h"
#include "socket.h"

namespace xkernel {

class UdpServer : public Server {
public:
    using Ptr = std::shared_ptr<UdpServer>;
    using PeerIdType = std::string;
    using onCreateSocket = std::function<Socket::Ptr(
        const EventPoller::Ptr&, const Buffer::Ptr&, struct sockaddr*, int)>;
    
    explicit UdpServer(const EventPoller::Ptr& poller = nullptr);
    ~UdpServer() override;

public:
    template <typename SessionType>
    void start(uint16_t port, const std::string& host = "::",
               const std::function<void(std::shared_ptr<SessionType>&)>& cb = nullptr) {
        static std::string cls_name = xkernel::demangle(typeid(SessionType).name());
        session_alloc_ = [cb](const UdpServer::Ptr& server, const Socket::Ptr& sock) {
            auto session = std::shared_ptr<SessionType>(
                new SessionType(sock), [cls_name](SessionType* ptr) {
                    TraceP(static_cast<Session*>(ptr)) << "~" << cls_name;
                    delete ptr;
                });
            if (cb) {
                cb(session);
            }
            TraceP(static_cast<Session*>(session.get())) << cls_name;
            auto sock_creator = server->on_create_socket_;
            session->setOnCreateSocket([sock_creator](const EventPoller::Ptr& poller) {
                return sock_creator(poller, nullptr, nullptr, 0);
            });
            return std::make_shared<SessionHelper>(server, std::move(session), cls_name);
        };
        start_l(port, host);
    }

    uint16_t getPort();
    void setOnCreateSocket(onCreateSocket cb);

protected:
    virtual Ptr onCreateServer(const EventPoller::Ptr& poller);
    virtual void cloneFrom(const UdpServer& that);

private:
    void start_l(uint16_t port, const std::string& host = "::");
    void onManagerSession();
    void onRead(Buffer::Ptr& buf, struct sockaddr* addr, int addr_len);
    void OnRead_l(bool is_server_fd, const PeerIdType& id, Buffer::Ptr& buf,
                  struct sockaddr* addr, int addr_len);
    SessionHelper::Ptr getOrCreateSession(const PeerIdType& id, Buffer::Ptr& buf,
                                          struct sockaddr* addr, int addr_len, bool& is_new);
    SessionHelper::Ptr createSession(const PeerIdType& id, Buffer::Ptr& buf,
                                     struct sockaddr* addr, int addr_len);
    Socket::Ptr createSocket(const EventPoller::Ptr& poller, const Buffer::Ptr& buf = nullptr,
                             struct sockaddr* addr = nullptr, int addr_len = 0);
    void setupEvent();

private:
    bool cloned_ = false;
    bool multi_poller_ = false;
    Socket::Ptr socket_;
    std::shared_ptr<Timer> timer_;
    onCreateSocket on_create_socket_;
    std::shared_ptr<std::recursive_mutex> session_mutex_;
    std::shared_ptr<std::unordered_map<PeerIdType, SessionHelper::Ptr>> session_map_;
    std::unordered_map<EventPoller*, Ptr> cloned_server_;
    std::function<SessionHelper::Ptr(
        const UdpServer::Ptr&, const Socket::Ptr&)> session_alloc_;
    ObjectCounter<UdpServer> counter_;
};

}  // namespace xkernel


#endif // _UDPSERVER_H_
