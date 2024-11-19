#ifndef _SSLBOX_H_
#define _SSLBOX_H_

#include <functional>
#include <mutex>
#include <string>
#include <map>

#include "buffer.h"
#include "resourcepool.h"

using X509 = struct x509_st;  // 数字证书
using EVP_PKEY = struct evp_pkey_st;  // 密钥
using SSL_CTX = struct ssl_ctx_st;  // SSL上下文
using SSL = struct ssl_st;  // SSL，表示一个SSL连接
using BIO = struct bio_st;  // BIO，I/O抽象层

namespace xkernel {
    
class SSLInitor {
public:
    friend class SSLBox;
    static SSLInitor& Instance();

public:
    bool loadCertificate(const std::string& pem_or_p12, bool server_mode = true,
                         const std::string& password = "", bool is_file = true,
                         bool is_default = true);
    void ignoreInvalidCertificate(bool ignore = true);
    bool trustCertificate(const std::string& pem_p12_cer, bool server_mode = false,
                           const std::string& password = "", bool is_file = true);
    bool trustCertificate(X509* cer, bool server_mode = false);
    std::shared_ptr<SSL_CTX> getSSLCtx(const std::string& vhost, bool server_mode);

private:
    SSLInitor();
    ~SSLInitor();

    std::shared_ptr<SSL> makeSSL(bool server_mode);
    bool setContext(const std::string& vhost, const std::shared_ptr<SSL_CTX>& ctx,
                    bool server_mode, bool is_default = true);
    void setupCtx(SSL_CTX* ctx);
    std::shared_ptr<SSL_CTX> getSSLCtx_l(const std::string& vhost, bool server_mode);
    std::shared_ptr<SSL_CTX> getSSLCtxWildcards(const std::string& vhost, bool server_mode);
    std::string defaultVhost(bool server_mode);
    static int findCertificate(SSL* ssl, int* ad, void* arg);

private:
    struct less_nocase {
        bool operator()(const std::string& x, const std::string& y) const {
            return strcasecmp(x.data(), y.data()) < 0;
        }
    };

private:
    std::string default_vhost_[2];  // 默认虚拟主机名, 0:server, 1:client
    std::shared_ptr<SSL_CTX> ctx_empty_[2];  // 空SSL上下文, 0:server, 1:client
    std::map<std::string, std::shared_ptr<SSL_CTX>, less_nocase> ctxs_[2];  // 虚拟主机名到SSL上下文的映射, 0:server, 1:client
    std::map<std::string, std::shared_ptr<SSL_CTX>, less_nocase> ctxs_wildcards_[2];  // 通配符虚拟主机名到SSL上下文的映射, 0:server, 1:client
};

class SSLBox {
public:
    SSLBox(bool server_mode = true, bool enable = true, int buff_size = 32 * 1024);
    ~SSLBox();

public:
    void onRecv(const Buffer::Ptr& buffer);  // 处理接收数据，进行解密
    void onSend(Buffer::Ptr buffer);  // 执行SSL/TLS握手过程，将数据缓冲区添加到发送缓冲区列表
    void setOnDecData(const std::function<void(const Buffer::Ptr&)>& cb);  // 设置on_dec_回调
    void setOnEncData(const std::function<void(const Buffer::Ptr&)>& cb);  // 设置on_enc_回调
    void shutdown();  // 关闭SSL连接
    void flush();  // 刷新Bio缓冲
    bool setHost(const char* host);

private:
    void flushWriteBio();
    void flushReadBio();

private:
    bool server_mode_;  // 是否为服务器模式
    bool send_handshake_;  // 是否已发送握手请求
    bool is_flush_;  // 是否刷新缓冲区
    int buff_size_;  // 缓冲区大小
    BIO* read_bio_;  // 存放接收数据的BIO缓冲区
    BIO* write_bio_;  // 存放发送数据的BIO缓冲区
    std::shared_ptr<SSL> ssl_;  // SSL对象，用于管理SSL/TLS连接
    List<Buffer::Ptr> buffer_send_;  // 发送缓冲区
    ResourcePool<BufferRaw> buffer_pool_;  // 缓冲区资源池
    std::function<void(const Buffer::Ptr&)> on_dec_;  // 解密后的回调
    std::function<void(const Buffer::Ptr&)> on_enc_;  // 加密后的回调
};

}  // namespace xkernel

#endif