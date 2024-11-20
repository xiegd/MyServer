#include "session.h"

#include <atomic>
#include "utility.h"

namespace xkernel {

class TcpSession : public Session {};
class UdpSession : public Session {};

STATISTIC_IMPL(TcpSession)
STATISTIC_IMPL(UdpSession)

Session::Session(const Socket::Ptr& sock) : SocketHelper(sock) {
    if (sock->sockType() == SockNum::SockType::TCP) {
        tcp_counter_.reset(new ObjectCounter<TcpSession>);
    } else {
        udp_counter_.reset(new ObjectCounter<UdpSession>);
    }
}

std::string Session::getIdentifier() const {
    if (id_.empty()) {
        static std::atomic<uint64_t> s_session_index{0};
        id_ = std::to_string(++s_session_index) + '-' + std::to_string(getSock()->rawFd());
    }
    return id_;
}

}  // namespace xkernel