#include "sockutil.h"

#include <sys/socket.h>
#include <assert.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <string>
#include <unordered_map>
#include <mutex>
#include <time.h>
#include <unistd.h>

#include "logger.h"
#include "utility.h"
#include "uv_errno.h"

namespace xkernel {

static int bind_sock(int fd, const char* ifr_ip, uint16_t port, int family);
//////////////////////////////////// DnsCache //////////////////////////////////
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
    struct addrinfo* getPerferredAddress(struct addrinfo* answer, int ai_family,
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

///////////////////////////////// create socket //////////////////////////////////

int SockUtil::connect(const char* host, uint16_t port, bool async, 
                      const char* local_ip, uint16_t local_port) {
    sockaddr_storage addr;
    if (!getDomainIP(host, port, addr, AF_INET, SOCK_STREAM, IPPROTO_TCP)) {
        return -1;
    }

    int sockfd = static_cast<int>(socket(addr.ss_family, SOCK_STREAM, IPPROTO_TCP));
    if (sockfd < 0) {
        WarnL << "Create socket failed: " << host;
        return -1;
    }

    setReuseable(sockfd);
    setNoSigpipe(sockfd);
    setNoBlocked(sockfd, async);
    setNoDelay(sockfd);
    setSendBuf(sockfd);
    setRecvBuf(sockfd);
    setCloseWait(sockfd);
    setCloExec(sockfd);

    if (bind_sock(sockfd, local_ip, local_port, addr.ss_family) == -1) {
        close(sockfd);
        return -1;
    }

    if (::connect(sockfd, reinterpret_cast<sockaddr*>(&addr), getSockLen(reinterpret_cast<sockaddr*>(&addr))) == 0) {
        return sockfd;  // 同步连接成功
    }
    if (async && get_uv_error(true) == UV_EAGAIN) {
        return sockfd;  // 异步连接成功
    }
    WarnL << "Connect socket to " << host << " " << port << " failed: " << get_uv_errmsg(true);
    close(sockfd);
    return -1;
}

// 创建tcp监听套接字
int SockUtil::listen(const uint16_t port, const char* local_ip, int back_log) {
    int fd = -1;
    int family = supportIpv6() ? (isIpv4(local_ip) ? AF_INET : AF_INET6) : AF_INET;
    if (fd = static_cast<int>(socket(family, SOCK_STREAM, IPPROTO_TCP)) == -1) {
        WarnL << "Create socket failed: " << get_uv_errmsg(true);
        return -1;
    }

    setReuseable(fd, true, false);
    setNoBlocked(fd);
    setCloExec(fd);

    if (bind_sock(fd, local_ip, port, family) == -1) {
        WarnL << "Bind socket failed: " << get_uv_errmsg(true);
        close(fd);
        return -1;
    }

    if (::listen(fd, back_log) == -1) {
        WarnL << "Listen socket failed: " << get_uv_errmsg(true);
        return -1;
    }
    return fd;
}

// 创建udp套接字
int SockUtil::bindUdpSock(const uint16_t port, const char* local_ip, bool enable_reuse) {
    int fd = -1;
    int family = supportIpv6() ? (isIpv4(local_ip) ? AF_INET : AF_INET6) : AF_INET;
    if ((fd = static_cast<int>(socket(family, SOCK_DGRAM, IPPROTO_UDP))) == -1) {
        WarnL << "Create socket failed: " << get_uv_errmsg();
        return -1;
    }

    setNoSigpipe(fd);
    setNoBlocked(fd);
    setSendBuf(fd);
    setRecvBuf(fd);
    setCloseWait(fd);
    setCloExec(fd);

    if (bind_sock(fd, local_ip, port, family) == -1) {
        WarnL << "Bind socket failed: " << get_uv_errmsg();
        close(fd);
        return -1;
    }
    return fd;
}

// 解除与udp sock相关的绑定关系    
int SockUtil::dissolveUdpSock(int fd) {
    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);
    if (-1 == getsockname(fd, reinterpret_cast<struct sockaddr*>(&addr), &addr_len)) {
        WarnL << "getsockname failed: " << get_uv_errmsg();
        return -1;
    }
    addr.ss_family = AF_UNSPEC;
    // 重新连接，重置socket连接状态
    if (-1 == ::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), addr_len)) {
        WarnL << "Connect socket AF_UNSPEC failed: " << get_uv_errmsg();
        return -1;
    }
    return 0;
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
                reinterpret_cast<sockaddr_in6*>(&addr)->sin6_port = htons(port);
                break;
            default:
                break;
        }
    }
    return flag;
}

// -------------------------------configure socket----------------------------------

// 配置tcp的nodelay特性
int SockUtil::setNoDelay(int fd, bool on) {
    int opt = on ? 1 : 0;
    int ret = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, static_cast<socklen_t>(sizeof(opt)));
    if (ret == -1) {
        TraceL << "setsockopt TCP_NODELAY failed";
    }
    return ret;
}

// 设置写socket不触发SIG_PIPE信号(貌似只有mac有效)
int SockUtil::setNoSigpipe(int fd) {
#if defined(SO_NOSIGPIPE)
    int set = 1;
    auto ret = setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &set, sizeof(int));
    if (ret == -1) {
        TraceL << "setsockopt SO_NOSIGPIPE failed";
    }
    return ret;
#else
    return -1;
#endif
}

// 设置读写socket是否阻塞
int SockUtil::setNoBlocked(int fd, bool noblock) {
    int flags = fcntl(fd, F_GETFL);
    if (flags == -1) {
        TraceL << "fcntl F_GETFL failed";
        return -1;
    }
    flags = noblock ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    int ret = fcntl(fd, F_SETFL, flags);
    if (ret == -1) {
        TraceL << "fcntl F_SETFL failed";
        return -1;
    }
    return ret;
}

int SockUtil::setRecvBuf(int fd, int size) {
    if (size <= 0) {
        return 0;
    }
    int ret = setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, static_cast<socklen_t>(sizeof(size)));
    if (ret == -1) {
        TraceL << "setsockopt SO_RCVBUF failed";
    }
    return ret;
}

int SockUtil::setSendBuf(int fd, int size) {
    if (size <= 0) {
        return 0;
    }
    int ret = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, static_cast<socklen_t>(sizeof(size)));
    if (ret == -1) {
        TraceL << "setsockopt SO_SNDBUF failed";
    }
    return ret;
}

// 配置后续可绑定复用的端口
int SockUtil::setReuseable(int fd, bool on, bool reuse_port) {
    int opt = on ? 1 : 0;
    int ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, static_cast<socklen_t>(sizeof(opt)));
    if (ret == -1) {
        TraceL << "setsockopt SO_REUSEADDR failed";
        return ret;
    }
#if defined(SO_REUSEPORT)
    if (reuse_port) {
        ret = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, static_cast<socklen_t>(sizeof(opt)));
        if (ret == -1) {
            TraceL << "setsockopt SO_REUSEPORT failed";
        }
    }
#endif
    return ret;
}

// 配置是否开启FD_CLOEXEC特性(多进程相关)
int SockUtil::setCloExec(int fd, bool on) {
    int flags = fcntl(fd, F_GETFD);
    if (flags == -1) {
        TraceL << "fcntl F_GETFD failed";
        return -1;
    }
    flags = on ? (flags | FD_CLOEXEC) : (flags & ~FD_CLOEXEC);
    int ret = fcntl(fd, F_SETFD, flags);
    if (ret == -1) {
        TraceL << "fcntl F_SETFD failed";
        return -1;
    }
    return ret;
}

// 设置socket关闭等待时间, 如果关闭时还有数据未发送完，允许等待second秒
int SockUtil::setCloseWait(int fd, int second) {
    linger m_sLinger;
    m_sLinger.l_onoff = (second > 0);
    m_sLinger.l_linger = second;
    int ret = setsockopt(fd, SOL_SOCKET, SO_LINGER, &m_sLinger, static_cast<socklen_t>(sizeof(m_sLinger)));
    if (ret == -1) {
        TraceL << "setsockopt SO_LINGER failed";
    }
    return ret;
}

// 配置是否允许发送或接收udp广播信息
int setBroadcast(int fd, bool on) {
    int opt = on ? 1 : 0;
    int ret = setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &opt, static_cast<socklen_t>(sizeof(opt)));
    if (ret == -1) {
        TraceL << "setsockopt SO_BROADCAST failed";
    }
    return ret;
}

// 配置tcp的keepalive特性
int setKeepAlive(int fd, bool on, int interval, int idle, int times) {
    int opt = on ? 1 : 0;
    int ret = setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &opt, static_cast<socklen_t>(sizeof(opt)));
    if (ret == -1) {
        TraceL << "setsockopt SO_KEEPALIVE failed";
    }
    return ret;
}

// 获取socket当前发生的错误的error code
int SockUtil::getSockError(int fd) {
    int opt;
    socklen_t opt_len = sizeof(opt);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &opt, &opt_len) < 0) {
        return get_uv_error(true);
    }
    return uv_translate_posix_error(opt);
}


//----------------------------------Configure Multicast----------------------------------

int SockUtil::setMultiTTL(int sock, uint8_t ttl) {
    int ret = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, static_cast<socklen_t>(sizeof(ttl)));
    if (ret == -1) {
        TraceL << "setsockopt IP_MULTICAST_TTL failed";
    }
    return ret;
}

int SockUtil::setMultiIF(int sock, const char* local_ip) {
    struct in_addr addr;
    addr.s_addr = inet_addr(local_ip);
    int ret = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &addr, sizeof(addr));
    if (ret == -1) {
        TraceL << "setsockopt IP_MULTICAST_IF failed";
    }
    return ret;
}

// 设置是否接收本机发出的组播包
int SockUtil::setMultiLoop(int sock, bool recv) {
    int opt = recv ? 1 : 0;
    int ret = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &opt, static_cast<socklen_t>(sizeof(opt)));
    if (ret == -1) {
        TraceL << "setsockopt IP_MULTICAST_LOOP failed";
    }
    return ret;
}

int SockUtil::joinMultiAddr(int fd, const char* addr, const char* local_ip) {
    struct ip_mreq imr;
    imr.imr_multiaddr.s_addr = inet_addr(addr);
    imr.imr_interface.s_addr = inet_addr(local_ip);
    int ret = setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &imr, sizeof(struct ip_mreq));
    if (ret == -1) {
        TraceL << "setsockopt IP_ADD_MEMBERSHIP failed: " << get_uv_errmsg(true);
    }
    return ret;
}

int SockUtil::leaveMultiAddr(int fd, const char* addr, const char* local_ip) {
    struct ip_mreq imr;
    imr.imr_multiaddr.s_addr = inet_addr(addr);
    imr.imr_interface.s_addr = inet_addr(local_ip);
    int ret = setsockopt(fd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &imr, sizeof(struct ip_mreq));
    if (ret == -1) {
        TraceL << "setsockopt IP_DROP_MEMBERSHIP failed: " << get_uv_errmsg(true);
    }
    return ret;
}

template<typename A, typename B>
static inline void write4Byte(A&& a, B&& b) {
    memcpy(&a, &b, sizeof(a));
}

// 加入组播并只接受来自指定源端的组播数据
int SockUtil::joinMultiAddrFilter(int sock, const char* addr, const char* src_ip, const char* local_ip) {
    struct ip_mreq_source imr;
    write4Byte(imr.imr_multiaddr, inet_addr(addr));
    write4Byte(imr.imr_sourceaddr, inet_addr(src_ip));
    write4Byte(imr.imr_interface, inet_addr(local_ip));

    int ret = setsockopt(sock, IPPROTO_IP, IP_ADD_SOURCE_MEMBERSHIP, &imr, sizeof(struct ip_mreq_source));
    if (ret == -1) {
        TraceL << "setsockopt IP_ADD_SOURCE_MEMBERSHIP failed: " << get_uv_errmsg(true);
    }
    return ret;
}

// 退出组播
int SockUtil::leaveMultiAddrFilter(int sock, const char* addr, const char* src_ip, const char* local_ip) {
    struct ip_mreq_source imr;
    write4Byte(imr.imr_multiaddr, inet_addr(addr));
    write4Byte(imr.imr_sourceaddr, inet_addr(src_ip));
    write4Byte(imr.imr_interface, inet_addr(local_ip));

    int ret = setsockopt(sock, IPPROTO_IP, IP_DROP_SOURCE_MEMBERSHIP, &imr, sizeof(struct ip_mreq_source));
    if (ret == -1) {
        TraceL << "setsockopt IP_DROP_SOURCE_MEMBERSHIP failed: " << get_uv_errmsg(true);
    }
    return ret;
}

///////////////////////////////// ip地址, 端口， 网卡的一些操作 /////////////////////////////////

template <typename T>
void for_each_netAdapter_posix(T&& fun) {
    struct ifconf ifconf;
    char buf[1024 * 10];
    ifconf.ifc_len = sizeof(buf);
    ifconf.ifc_buf = buf;
    int sockfd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        WarnL << "Create socket failed: " << get_uv_errmsg();
        return ;
    }
    if ( -1 == ioctl(sockfd, SIOCGIFCONF, &ifconf)) {
        WarnL << "ioctl SIOCGIFCONF failed: " << get_uv_errmsg();
        close(sockfd);
        return ;
    }
    close(sockfd);
    struct ifreq* adapter = reinterpret_cast<struct ifreq*>(buf);
    for (int i = (ifconf.ifc_len / sizeof(struct ifreq)); i > 0; --i, ++adapter) {
        if (fun(adapter)) {
            break;
        }
    }
}

// 获取网卡列表
std::vector<std::map<std::string, std::string>> SockUtil::getInterfaceList() {
    std::vector<std::map<std::string, std::string>> ret;
    for_each_netAdapter_posix([&](struct ifreq* adapter){
        std::map<std::string, std::string> ifr_info;
        ifr_info["ip"]  = SockUtil::inetNtoa(&(adapter->ifr_addr));
        ifr_info["name"] = adapter->ifr_name;
        ret.emplace_back(std::move(ifr_info));
        return false;
    });
    return ret;
}

inline bool SockUtil::supportIpv6() {
    static bool flag = []() -> bool {
        auto fd = socket(AF_INET6, SOCK_STREAM, 0);
        if (fd == -1) {
            return false;
        }
        close(fd);
        return true;
    };
    return flag;
}

static inline std::string inet_ntop_l(int af, const void* addr) {
    std::string ret;
    ret.resize(128);
    if (!inet_ntop(af, const_cast<void*>(addr), ret.data(), ret.size())) {
        ret.clear();
    } else {
        ret.resize(strlen(ret.data()));
    }
    return ret;
}

std::string SockUtil::inetNtoa(const struct in_addr& addr) { return inet_ntop_l(AF_INET, &addr); }

std::string SockUtil::inetNtoa(const struct in6_addr& addr) { return inet_ntop_l(AF_INET6, &addr); }

std::string SockUtil::inetNtoa(const struct sockaddr* addr) {
    switch (addr->sa_family) {
        case AF_INET:
            return inetNtoa(reinterpret_cast<const struct sockaddr_in*>(addr)->sin_addr);
        case AF_INET6: {
            if (IN6_IS_ADDR_V4MAPPED(&reinterpret_cast<const struct sockaddr_in6*>(addr)->sin6_addr)) {
                struct in_addr addr4;
                memcpy(&addr4, 12 + (char*)&reinterpret_cast<const struct sockaddr_in6*>(addr)->sin6_addr, 4);
                return inetNtoa(addr4);
            }
            return inetNtoa(reinterpret_cast<const struct in6_addr&>(addr));
        }
        default:
            assert(0);
            return "";
    }
}

uint16_t SockUtil::inetPort(const struct sockaddr* addr) {
    switch (addr->sa_family) {
        case AF_INET:
            return ntohs(((struct sockaddr_in*)addr)->sin_port);
        case AF_INET6:
            return ntohs(((struct sockaddr_in6*)addr)->sin6_port);
        default:
            return 0;
    }
}

struct sockaddr_storage SockUtil::makeSockAddr(const char* host, uint16_t port) {
    struct sockaddr_storage storage;
    bzero(&storage, sizeof(storage));

    struct in_addr addr;
    struct in6_addr addr6;
    if (1 == inet_pton(AF_INET, host, &addr)) {
        reinterpret_cast<struct sockaddr_in&>(storage).sin_addr = addr;
        reinterpret_cast<struct sockaddr_in&>(storage).sin_family = AF_INET;
        reinterpret_cast<struct sockaddr_in&>(storage).sin_port = htons(port);
        return storage;
    }
    if (1 == inet_pton(AF_INET6, host, &addr6)) {
        reinterpret_cast<struct sockaddr_in6&>(storage).sin6_addr = addr6;
        reinterpret_cast<struct sockaddr_in6&>(storage).sin6_family = AF_INET6;
        reinterpret_cast<struct sockaddr_in6&>(storage).sin6_port = htons(port);
        return storage;
    }
    throw std::invalid_argument(std::string("Not ip address: ") + host);
}

// 根据网卡名获取ip
std::string SockUtil::getIfrIP(const char* if_name) {
    std::string ret;
    for_each_netAdapter_posix([&](struct ifreq* adapter) {
        if (strcmp(adapter->ifr_name, if_name) == 0) {
            ret = inetNtoa(&(adapter->ifr_addr));
            return true;
        }
        return false;
    });
    return ret;
}

// 根据ip获取网卡名
std::string SockUtil::getIfrName(const char* local_ip) {
    std::string ret = "en0";
    for_each_netAdapter_posix([&](struct ifreq* adapter) {
        std::string ip = inetNtoa(&(adapter->ifr_addr));
        if (ip == local_ip) {
            ret = adapter->ifr_name;
            return true;
        }
        return false;
    });
    return ret;
}

// 根据网卡名获取子网掩码
std::string SockUtil::getIfrMask(const char* if_name) {
    struct ifreq ifr_mask;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        WarnL << "create socket failed: " << get_uv_errmsg();
        return "";
    }
    memset(&ifr_mask, 0, sizeof(ifr_mask));
    strncpy(ifr_mask.ifr_name, if_name, sizeof(ifr_mask.ifr_name) - 1);
    if (ioctl(fd, SIOCGIFNETMASK, &ifr_mask) < 0) {
        WarnL << "ioctl SIOCGIFNETMASK failed: " << get_uv_errmsg();
        close(fd);
        return "";
    }
    close(fd);
    return inetNtoa(&(ifr_mask.ifr_netmask));
}

// 根据网卡名获取广播地址
std::string getIfrBrdAddr(const char* if_name) {
    struct ifreq ifr_brdaddr;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        WarnL << "Create socket failed: " << get_uv_errmsg();
        return "";
    }
    memset(&ifr_brdaddr, 0, sizeof(ifr_brdaddr));
    strncpy(ifr_brdaddr.ifr_name, if_name, sizeof(ifr_brdaddr.ifr_name) - 1);
    if (ioctl(fd, SIOCGIFBRDADDR, &ifr_brdaddr) < 0) {
        WarnL << "ioctl SIOCGIFBRDADDR failed: " << get_uv_errmsg();
        close(fd);
        return "";
    }
    close(fd);
    return SockUtil::inetNtoa(&(ifr_brdaddr.ifr_broadaddr));
}

// 判断两个ip是否为同一网段
bool SockUtil::inSameLan(const char* src_ip, const char* dts_ip) {
#define ip_addr_netcmp(addr1, addr2, mask) \
    (((addr1) & (mask)) == ((addr2) & (mask)))
    
    std::string mask = getIfrMask(getIfrName(src_ip).data());
    return ip_addr_netcmp(inet_addr(src_ip), inet_addr(dts_ip), inet_addr(mask.data()));
}

// 判断是否为ipv4地址 
bool SockUtil::isIpv4(const char* host) {
    struct in_addr addr;
    return 1 == inet_pton(AF_INET, host, &addr);
}

// 判断是否为ipv6地址
bool SockUtil::isIpv6(const char* host) {
    struct in6_addr addr;
    return 1 == inet_pton(AF_INET6, host, &addr);
}

bool SockUtil::isIP(const char* host) {
    return isIpv4(host) || isIpv6(host);
}

socklen_t SockUtil::getSockLen(const struct sockaddr* addr) {
    switch(addr->sa_family) {
        case AF_INET:
            return sizeof(sockaddr_in);
        case AF_INET6:
            return sizeof(sockaddr_in6);
        default:
            assert(0);
            return 0;
    }
}

using getsockname_type = decltype(getsockname);
// 根据传入的func, fd获取相应的地址信息存到sockaddr_storage中
static bool get_socket_addr(int fd, struct sockaddr_storage& addr, getsockname_type func) {
    socklen_t addr_len = sizeof addr;
    if (-1 == func(fd, reinterpret_cast<sockaddr*>(&addr), &addr_len)) {
        WarnL << "get socket addr failed: " << get_uv_errmsg();
        return false;
    }
    return true;
}

bool SockUtil::getSockLocalAddr(int fd, struct sockaddr_storage& addr) {
    return get_socket_addr(fd, addr, getsockname);
}

bool SockUtil::getSockPeerAddr(int fd, struct sockaddr_storage& addr) {
    return get_socket_addr(fd, addr, getpeername);
}

static std::string get_socket_ip(int fd, getsockname_type func) {
    struct sockaddr_storage addr;
    if (!get_socket_addr(fd, addr, func)) {
        return "";
    }
    return SockUtil::inetNtoa(reinterpret_cast<struct sockaddr*>(&addr));
}

static uint16_t get_socket_port(int fd, getsockname_type func) {
    struct sockaddr_storage addr;
    if (!get_socket_addr(fd, addr, func)) {
        return 0;
    }
    return SockUtil::inetPort(reinterpret_cast<struct sockaddr*>(&addr));
}

// 检查ip是否为私有地址
bool check_iP(std::string& address, const std::string& ip) {
    if (ip != "127.0.0.1" && ip != "0.0.0.0") {
        address = ip;
        uint32_t addressInNetworkOrder = ntohl(inet_addr(ip.data()));
        // A, B, C类私有地址
        if ((addressInNetworkOrder >= 0x0A000000 && addressInNetworkOrder < 0x0E000000) ||
            (addressInNetworkOrder >= 0xAC100000 && addressInNetworkOrder < 0xAC200000) ||
            (addressInNetworkOrder >= 0xC0A80000 && addressInNetworkOrder < 0xC0A90000)) {
            return true;
        }
    }
    return false;
}

// 获取本机默认网卡ip
std::string SockUtil::getLocalIp() {
    std::string address = "127.0.0.1";
    for_each_netAdapter_posix([&](struct ifreq* adapter) {
        std::string ip = SockUtil::inetNtoa(&(adapter->ifr_addr));
        // 跳过docker网卡
        if (strstr(adapter->ifr_name, "docker")) {
            return false;
        }
        return check_iP(address, ip);
    });
    return address;
}

// 获取socket绑定的本地ip
std::string SockUtil::getLocalIp(int fd) { return get_socket_ip(fd, getsockname); }
// 获取socket绑定的远端ip
std::string SockUtil::getPeerIp(int fd) { return get_socket_ip(fd, getpeername); }
// 获取socket绑定的本地端口
uint16_t SockUtil::getLocalPort(int fd) { return get_socket_port(fd, getsockname); }
// 获取socket绑定的远端端口
uint16_t SockUtil::getPeerPort(int fd) { return get_socket_port(fd, getpeername); }

static int bind_sock4(int fd, const char* ifr_ip, uint16_t port) {
    struct sockaddr_in addr;
    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (1 != inet_pton(AF_INET, ifr_ip, &(addr.sin_addr))) {
        if (strcmp(ifr_ip, "::")) {
            WarnL << "inet_pton to ipv4 address failed: " << ifr_ip;
        }
        addr.sin_addr.s_addr = INADDR_ANY;
    }
    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == -1) {
        WarnL << "Bind socket failed: " << get_uv_errmsg();
        return -1;
    }
    return 0;
}

static int set_ipv6_only(int fd, bool flag) {
    int opt = flag;
    int ret = setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
    if (ret == -1) {
        WarnL << "setsockopt IPV6_V6ONLY failed";
    }
    return ret;
}

static int bind_sock6(int fd, const char* ifr_ip, uint16_t port) {
    set_ipv6_only(fd, false);
    struct sockaddr_in6 addr;
    bzero(&addr, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(port);
    if (1 != inet_pton(AF_INET6, ifr_ip, &(addr.sin6_addr))) {
        if (strcmp(ifr_ip, "0.0.0.0")) {
            WarnL << "inet_pton to ipv6 address failed: " << ifr_ip;
        }
        addr.sin6_addr = IN6ADDR_ANY_INIT;
    }
    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == -1) {
        WarnL << "Bind socket failed: " << get_uv_errmsg();
        return -1;
    }
    return 0;
}

static int bind_sock(int fd, const char* ifr_ip, uint16_t port, int family) {
    switch (family) {
        case AF_INET:
            return bind_sock4(fd, ifr_ip, port);
        case AF_INET6:
            return bind_sock6(fd, ifr_ip, port);
        default:
            return -1;
    }
}

DnsCache& DnsCache::Instance() {
    static DnsCache instance;
    return instance;
}

bool DnsCache::getDomainIP(const char* host, sockaddr_storage& storage,
                          int ai_family, int ai_socktype, int ai_protocol,
                          int expire_secs) {
    try {
        storage = SockUtil::makeSockAddr(host, 0);  // 如果传入的是IP地址， 则转换后返回
        return true;
    } catch (...) {
        // 否则是作为传入的域名，进行dns解析
        auto item = getCacheDomainIP(host, expire_secs);
        if (!item) {
            item = getSystemDomainIP(host);
            if (item) {
                setCacheDomainIP(host, item);
            }
        }
        if (item) {
            auto addr = getPerferredAddress(item.get(), ai_family, ai_socktype, ai_protocol);
            memcpy(&storage, addr->ai_addr, addr->ai_addrlen);
        }
        return static_cast<bool>(item);  // 解析失败返回false
    }
}

std::shared_ptr<struct addrinfo> DnsCache::getCacheDomainIP(const char* host, int expireSec) {
    std::lock_guard<std::mutex> lck(mtx_);
    auto it = dns_cache_.find(host);
    if (it == dns_cache_.end()) {
        return nullptr;
    }
    if (it->second.create_time_ + expireSec < time(nullptr)) {
        dns_cache_.erase(it);
        return nullptr;
    }
    return it->second.addr_info_;
}

void DnsCache::setCacheDomainIP(const char* host, std::shared_ptr<struct addrinfo> addr) {
    std::lock_guard<decltype(mtx_)> lck(mtx_);
    DnsItem item;
    item.addr_info_ = std::move(addr);
    item.create_time_ = time(nullptr);
    dns_cache_[host] = std::move(item);
}

std::shared_ptr<struct addrinfo> DnsCache::getSystemDomainIP(const char* host) {
    struct addrinfo* answer = nullptr;
    int ret = -1;
    // 处理getaddrinfo被中断的情况
    do {
        ret = getaddrinfo(host, nullptr, nullptr, &answer);
    } while (ret == -1 && get_uv_error(true) == UV_EINTR);

    if (!answer) {
        WarnL << "getaddrinfo failed: " << host;
    }
    return std::shared_ptr<struct addrinfo>(answer, freeaddrinfo);
}

struct addrinfo* DnsCache::getPerferredAddress(struct addrinfo* answer, int ai_family, 
                                    int ai_socktype, int ai_protocol) {
    auto ptr = answer;
    while (ptr) {
        if (ptr->ai_family == ai_family &&
            ptr->ai_socktype == ai_socktype &&
            ptr->ai_protocol == ai_protocol) {
            return ptr;
        }
        ptr = ptr->ai_next;
    }
    return answer;
}

}  // namespace xkernel
