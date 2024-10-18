#include "sockutil.h"

#include <sys/socket.h>
#include <unordered_map>
#include <mutex>

#include "logger.h"
#include "utility.h"
#include "uv_errno.h"

namespace xkernel {

class DnsCache;

int SockUtil::connect(const char* host, uint16_t port, bool async = true, 
                    const char* local_ip = "::", uint16_t local_port = 0) {
    sockaddr_storage addr;
    if (!getDomainIP(host, port, addr, AF_INET, SOCK_STREAM, IPPROTO_TCP)) {

    }
}

bool SockUtil::getDomainIP(const char* host, uint16_t port, 
                          struct sockaddr_storage& addr, int ai_family,
                          int ai_socktype, int ai_protocol, int expire_sec) {
    bool flag = DnsCache::Instance().getDomainIP(host, addr, ai_family, ai_socktype, ai_protocol, expire_sec);
    if (flag) {
        switch (addr.ss_family) {
            case AF_INET:
                reinterpret_cast<sockaddr_in*>(&addr)->sin_port = htons(port);
                break;
            case AF_INET6:
                reinterpret_cast<sockaddr_in6*>(&addr)->sin6_port = htos(port);
                break;
            default:
                break;
        }
    }
    return flag;
}


class DnsCache {
public:
    static DnsCache& Instance();

    bool getDomainIP(const char* host, sockaddr_storage& storage,
                     int ai_family = AF_INET, int ai_socktype = SOCK_STREAM,
                     int ai_protocol = IPPROTO_TCP, int expire_sec = 60);
private:
    std::shared_ptr<struct addrinfo> getCacheDomainIP(const char* host, int expireSec);
    void setCacheDomainIP(const char* host, std::shared_ptr<struct addrinfo> addr);
    std::shared_ptr<struct addrinfo> getSystemDomainIP(const char* host);
    struct addrinfo* getPreferredAddress(struct addrinfo* answer, int ai_family,
                                        int ai_socktype, int ai_protocol);

private:
    class DnsItem {
    public:
        std::shared_ptr<struct addrinfo> addr_info_;
        time_t create_time_;
    };
    std::mutex mtx_;
    std::unordered_map<std::string, DnsItem> dns_cache_;
};

DnsCache& DnsCache::Instance() {
    static DnsCache instance;
    return instance;
}

bool DnsCache::getDomainIP(const char* host, sockaddr_storage& storage,
                          int ai_family, int ai_socktype, int ai_protocol,
                          int expire_secs) {
    try {
        storage = SockUtil::makeSockAddr(host, 0);
        return true;
    } catch (...) {
        auto item = getCacheDomainIP(host, expire_secs);
        if (!item) {
            item = getSystemDomainIP(host);
            if (item) {
                setCacheDomainIP(host, item);
            }
        }
        if (item) {
            auto addr = getPreferredAddress(item.get(), ai_family, ai_socktype, ai_protocol);
            memcpy(&storage, addr->ai_addr, addr->ai_addrlen);
        }
        return static_cast<bool>(item);
    }
}

std::shared_ptr<struct addrinfo> DnsCache::getCacheDomainIP(const char* host, int expireSec) {
    std::lock_guard<std::mutex> lck(mtx_);
    auto it = dns_cache_.find(host);
    if (it == dns_cache_.end()) {
        return nullptr;
    }
    if (it->second.create_time_ + expireSec < time(nullptr))
}

}  // namespace xkernel
