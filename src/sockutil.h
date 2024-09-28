#ifndef _SOCKUTIL_H_
#define _SOCKUTIL_H_


#include <arpa/inet.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#define SOCKET_DEFAULT_BUF_SIZE (256 * 1024)
#define TCP_KEEPALIVE_INTERVAL (60)
#define TCP_KEEPALIVE_TIME (300)
#define TCP_KEEPALIVE_PROBE_TIMES (5)

// 套接字工具类，封装了socket、网络的一些基本操作
class SockUtil {
public:
    // 创建tcp客服端套接字并连接到服务器
    static int connect(const char* host, uint16_t port, bool async = true,
                       const char* local_ip = "::", uint16_t local_port = 0);
    // 创建tcp监听套接字
    static int listen(const uint16_t port, const char* local_ip = "::", int back_log = 1024);
    // 创建udp套接字
    static int bindUdpSock(const uint16_t port, const char* local_ip = "::", 
                        bool enable_reuse = true);
    // 解除与udp sock相关的绑定关系    
    static int dissolveUdpSock(int sock);
    // 配置tcp的nodelay特性
    static int setNoDelay(int fd, bool on = true);
    // 设置写socket不触发SIG_PIPE信号(貌似只有mac有效)
    static int setNoSigpipe(int fd);
    // 设置读写socket是否阻塞
    static int setNoBlocked(int fd, bool noblock = true);
    // 设置socket接收缓存大小
    static int setRecvBuf(int fd, int size = SOCKET_DEFAULT_BUF_SIZE);
    // 设置socket发送缓存大小
    static int setSendBuf(int fd, int size = SOCKET_DEFAULT_BUF_SIZE);
    // 配置后续可绑定复用的端口
    static int setReuseable(int fd, bool on = true, bool reuse_port = true);
    // 配置是否允许发送或接收udp广播信息
    static int setBroadcast(int fd, bool on = true);
    // 配置tcp的keepalive特性
    static int setKeepAlive(int fd, bool on = true,
                            int interval = TCP_KEEPALIVE_INTERVAL,
                            int idle = TCP_KEEPALIVE_TIME,
                            int times = TCP_KEEPALIVE_PROBE_TIMES);
    // 配置是否开启FD_CLOEXEC特性(多进程相关)
    static int setCloExec(int fd, bool on = true);
    // 设置socket关闭等待时间, 如果关闭时还有数据未发送完，允许等待second秒
    static int setCloseWait(int sock, int second = 0);
    // 进行dns解析
    static bool getDomainIP(const char* host, uint16_t port,
                            struct sockaddr_storage& addr,
                            int ai_family = AF_INET,
                            int ai_socktype = SOCK_STREAM,
                            int ai_protocol = IPPROTO_TCP, int expire_sec = 60);
    // 设置组播ttl
    static int setMultiTTL(int sock, uint8_t ttl = 64);
    // 设置组播发送网卡
    static int setMultiIF(int sock, const char* local_ip);
    // 设置是否接收本机发出的组播包
    static int setMultiLoop(int sock, bool recv = false);
    // 加入组播
    static int joinMultiAddr(int fd, const char* addr, const char* local_ip = "0.0.0.0");
    // 退出组播
    static int leaveMultiAddr(int fd, const char* addr, const char* local_ip = "0.0.0.0");
    // 加入组播并只接受来自指定源端的组播数据
    static int joinMultiAddrFilter(int sock, const char* addr, const char* src_ip, 
                                    const char* local_ip = "0.0.0.0");
    // 退出组播
    static int leaveMultiAddrFilter(int sock, const char* addr, const char* src_ip, 
                                    const char* local_ip = "0.0.0.0");
    // 获取socket当前发生的错误的error code
    static int getSockError(int fd);
    // 获取网卡列表
    static std::vector<std::map<std::string, std::string>> getInterfaceList();

    // 获取本机默认网卡ip
    static std::string getLocalIp();
    // 获取socket绑定的本地ip
    static std::string getLocalIp(int sock);
    // 获取socket绑定的本地端口
    static uint16_t getLocalPort(int sock);
    // 获取socket绑定的远端ip
    static std::string getPeerIp(int sock);
    // 获取socket绑定的远端端口
    static uint16_t getPeerPort(int sock);
    static bool supportIpv6();

    static std::string inetNtoa(const struct in_addr& addr);
    static std::string inetNtoa(const struct in6_addr& addr);
    static std::string inetNtoa(const struct sockaddr* addr);
    static uint16_t inetPort(const struct sockaddr* addr);
    static struct sockaddr_storage makeSockAddr(const char* ip, uint16_t port);
    static socklen_t getSockLen(const struct sockaddr* addr);
    static bool getSockLocalAddr(int fd, struct sockaddr_storage& addr);
    static bool getSockPeerAddr(int fd, struct sockaddr_storage& addr);

    // 根据网卡名获取ip
    static std::string getIfrIP(const char* if_name);
    // 根据ip获取网卡名
    static std::string getIfrName(const char* local_op);
    // 根据网卡名获取子网掩码
    static std::string getIfrMask(const char* if_name);
    // 根据网卡名获取广播地址
    static std::string getIfrBrdAddr(const char* if_name);
    // 判断两个ip是否为同一网段
    static bool inSameLan(const char* src_ip, const char* dts_ip);

    // 判断是否为ipv4地址 
    static bool isIpv4(const char* str);
    // 判断是否为ipv6地址
    static bool isIpv6(const char* str);

}

#endif // _SOCKUTIL_H_