#ifndef _SERVER_H_
#define _SERVER_H_

#include <unordered_map>
#include <mutex>

#include "session.h"
#include "ini.h"
#include "eventpoller.h"

namespace xkernel {

class SessionMap : public std::enable_shared_from_this<SessionMap> {
public:
    friend class SessionHelper;
    using Ptr = std::shared_ptr<SessionMap>;

    static SessionMap &Instance();
    ~SessionMap() = default;

public:
    Session::Ptr get(const std::string& tag);
    void forEachSession(const std::function<void(const std::string& id, const Session::Ptr& session)>& cb);

private:
    SessionMap() = default;

    bool del(const std::string& tag);
    bool add(const std::string& tag, const Session::Ptr& session);

private:
    std::mutex mtx_session_;
    std::unordered_map<std::string, std::weak_ptr<Session>> map_session_;
};

class Server;

class SessionHelper {
public:
    using Ptr = std::shared_ptr<SessionHelper>;

    SessionHelper(const std::weak_ptr<Server>& server, Session::Ptr session, const std::string cls);
    ~SessionHelper();

    const Session::Ptr& session() const;
    const std::string& className() const;

    bool enable = true;

private:
    std::string cls_;
    std::string identifier_;
    Session::Ptr session_;
    SessionMap::Ptr session_map_;
    std::weak_ptr<Server> server_;
};

// server 基类, 暂时仅用于剥离 SessionHelper 对 TcpServer 的依赖
// 这里还要继续改进
class Server : public std::enable_shared_from_this<Server>, public mIni {
public:
    using Ptr = std::shared_ptr<Server>;

    explicit Server(EventPoller::Ptr poller = nullptr);
    virtual ~Server() = default;

protected:
    EventPoller::Ptr poller_;
};

}  // namespace xkernel

#endif