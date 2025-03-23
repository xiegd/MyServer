#include "server.h"

namespace xkernel {

//////////////////////////////////// SessionMap //////////////////////////////////////////

INSTANCE_IMP(SessionMap);

Session::Ptr SessionMap::get(const std::string& tag) {
    std::lock_guard<decltype(mtx_session_)> lck(mtx_session_);
    auto it = map_session_.find(tag);
    if (it == map_session_.end()) {
        return nullptr;
    }
    return it->second.lock();
}

void SessionMap::forEachSession(const std::function<void(const std::string& id, 
                                const Session::Ptr& session)>& cb) {
    std::lock_guard<decltype(mtx_session_)> lck(mtx_session_);
    for (auto it = map_session_.begin(); it != map_session_.end(); ) {
        auto session = it->second.lock();
        if (!session) {
            it = map_session_.erase(it);
            continue;
        }
        cb(it->first, session);
        ++it;
    }

}

bool SessionMap::del(const std::string& tag) {
    std::lock_guard<decltype(mtx_session_)> lock(mtx_session_);
    return map_session_.erase(tag);
}

bool SessionMap::add(const std::string& tag, const Session::Ptr& session) {
    std::lock_guard<decltype(mtx_session_)> lock(mtx_session_);
    return map_session_.emplace(tag, session).second;
}

//////////////////////////////////// SessionHelper //////////////////////////////////////////

SessionHelper::SessionHelper(const std::weak_ptr<Server>& server, 
                             Session::Ptr session, const std::string cls) {
    InfoL << "enter SessionHelper construtor!";
    server_ = server;
    session_ = std::move(session);
    cls_ = std::move(cls);
    session_map_ = SessionMap::Instance().shared_from_this();
    identifier_ = session_->getIdentifier();
    InfoL << "before add!";
    session_map_->add(identifier_, session_);
    InfoL << "leave SessionHelper construtor!";
}

SessionHelper::~SessionHelper() {
    if (!server_.lock()) {
        session_->onErr(SockException());
    }
    session_map_->del(identifier_);
}

const Session::Ptr& SessionHelper::session() const { return session_; }

const std::string& SessionHelper::className() const { return cls_; }


//////////////////////////////////// Server //////////////////////////////////////////

Server::Server(EventPoller::Ptr poller) {
    poller_ = poller ? std::move(poller) : EventPollerPool::Instance().getPoller();
}

}  // namespace xkernel