#include <iostream>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <iomanip>

void print_addrinfo(const struct addrinfo* ai) {
    char ipstr[INET6_ADDRSTRLEN];
    void* addr;
    const char* ipver;
    uint16_t port = 0;

    std::cout << "Address Info:" << std::endl;
    std::cout << "  ai_flags: " << ai->ai_flags << std::endl;
    std::cout << "  ai_family: " << ai->ai_family << " (";
    
    if (ai->ai_family == AF_INET) {
        std::cout << "IPv4)" << std::endl;
        struct sockaddr_in* ipv4 = (struct sockaddr_in*)ai->ai_addr;
        addr = &(ipv4->sin_addr);
        port = ntohs(ipv4->sin_port);
    } else if (ai->ai_family == AF_INET6) {
        std::cout << "IPv6)" << std::endl;
        struct sockaddr_in6* ipv6 = (struct sockaddr_in6*)ai->ai_addr;
        addr = &(ipv6->sin6_addr);
        port = ntohs(ipv6->sin6_port);
    } else {
        std::cout << "Unknown)" << std::endl;
        return;
    }

    std::cout << "  ai_socktype: " << ai->ai_socktype << " (";
    switch(ai->ai_socktype) {
        case SOCK_STREAM: std::cout << "TCP"; break;
        case SOCK_DGRAM: std::cout << "UDP"; break;
        default: std::cout << "Other"; break;
    }
    std::cout << ")" << std::endl;

    std::cout << "  ai_protocol: " << ai->ai_protocol << std::endl;
    std::cout << "  ai_addrlen: " << ai->ai_addrlen << std::endl;

    inet_ntop(ai->ai_family, addr, ipstr, sizeof ipstr);
    std::cout << "  IP address: " << ipstr << std::endl;
    std::cout << "  Port: " << port << std::endl;

    if (ai->ai_canonname) {
        std::cout << "  Canonical name: " << ai->ai_canonname << std::endl;
    }

    // 打印原始地址数据
    std::cout << "  Raw address data:" << std::endl;
    unsigned char* p = (unsigned char*)ai->ai_addr;
    for (size_t i = 0; i < ai->ai_addrlen; ++i) {
        std::cout << "    " << std::setw(2) << std::setfill('0') << std::hex << (int)p[i];
        if ((i + 1) % 8 == 0 || i == ai->ai_addrlen - 1) std::cout << std::endl;
        else std::cout << " ";
    }
    std::cout << std::dec; // 重置为十进制输出

    std::cout << std::endl;
}

void get_addr_info(const char* ip, const struct addrinfo* hints) {
    struct addrinfo* res;
    int status = getaddrinfo(ip, NULL, hints, &res);
    if (status != 0) {
        std::cerr << "getaddrinfo error: " << gai_strerror(status) << std::endl;
        return;
    }

    for (struct addrinfo* p = res; p != NULL; p = p->ai_next) {
        print_addrinfo(p);
    }

    freeaddrinfo(res);
}

void printBytes(const char* label, uint32_t value) {
    std::cout << label << ": 0x" << std::hex << std::setfill('0') << std::setw(8) << value
              << " (";
    for (int i = 0; i < 4; ++i) {
        std::cout << std::dec << ((value >> (i * 8)) & 0xFF);
        if (i < 3) std::cout << ".";
    }
    std::cout << ")" << std::endl;
}

int main() {
    // const char* ip = "www.baidu.com";

    // std::cout << "1. No hints specified:" << std::endl;
    // get_addr_info(ip, NULL);

    // struct addrinfo hints;
    // memset(&hints, 0, sizeof hints);

    // std::cout << "2. IPv4 only:" << std::endl;
    // hints.ai_family = AF_INET;
    // get_addr_info(ip, &hints);

    // std::cout << "3. IPv6 only:" << std::endl;
    // hints.ai_family = AF_INET6;
    // get_addr_info(ip, &hints);

    // std::cout << "4. TCP only:" << std::endl;
    // hints.ai_family = AF_UNSPEC;
    // hints.ai_socktype = SOCK_STREAM;
    // get_addr_info(ip, &hints);

    // std::cout << "5. UDP only:" << std::endl;
    // hints.ai_socktype = SOCK_DGRAM;
    // get_addr_info(ip, &hints);

    // 测试16位整数
    uint16_t host_short = 0x1234;
    uint16_t net_short = htons(host_short);
    uint16_t back_to_host_short = htons(net_short);

    std::cout << "16-bit integer test:" << std::endl;
    std::cout << "Original: 0x" << std::hex << host_short << std::endl;
    std::cout << "After htons: 0x" << std::hex << net_short << std::endl;
    std::cout << "After ntohs: 0x" << std::hex << back_to_host_short << std::endl;
    std::cout << std::endl;

    // 测试32位整数
    uint32_t host_long = 0x12345678;
    uint32_t net_long = htonl(host_long);
    uint32_t back_to_host_long = htonl(net_long);

    std::cout << "32-bit integer test:" << std::endl;
    printBytes("Original", host_long);
    printBytes("After htonl", net_long);
    printBytes("After ntohl", back_to_host_long);

    return 0;
}
