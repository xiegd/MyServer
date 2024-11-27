#include "SSLbox.h"

#include <mutex>

#include "SSLutil.h"
#include "utility.h"
#include "logger.h"

#define ENABLE_OPENSSL

// 可选是否使用openssl加密通信, 默认openssl版本>1.1.0
#if defined(ENABLE_OPENSSL)
#include "openssl/bio.h"
#include "openssl/conf.h"
#include "openssl/crypto.h"
#include "openssl/err.h"
#include "openssl/ossl_typ.h"
#include "openssl/rand.h"
#include "openssl/ssl.h"
#endif  // defined(ENABLE_OPENSSL)

namespace xkernel {

///////////////////////////// SSLInitor /////////////////////////////

static bool s_ignore_invalid_cer = true;

SSLInitor& SSLInitor::Instance() {
    static SSLInitor obj;
    return obj;
}

SSLInitor::SSLInitor() {
#if defined(ENABLE_OPENSSL)
    SSL_library_init();  // 初始化SSL库
    SSL_load_error_strings();  // 加载错误字符串
    OpenSSL_add_all_digests();  // 添加所有摘要算法
    OpenSSL_add_all_ciphers();  // 添加所有加密算法
    OpenSSL_add_all_algorithms();  // 添加所有算法
    // 设置锁定回调函数, 在多线程下保护SSL相关的共享资源
    CRYPTO_set_locking_callback([](int mode, int n, const char* file, int line) {
        static std::mutex* s_mutexes = new std::mutex[CRYPTO_num_locks()];
        static onceToken token(nullptr, []() { delete[] s_mutexes; })
        if (mode & CRYPTO_LOCK) {
            s_mutexs[n].lock();
        } else {
            s_mutexs[n].unlock();
        }
    });
    // 设置获取线程ID回调函数
    CRYPTO_set_id_callback([]() -> unsigned long { return static_cast<unsigned long>(pthread_self()); }); 
    setContext("", SSLUtil::makeSSLContext(std::vector<std::shared_ptr<X509>>(), nullptr, false), false);
    setContext("", SSLUtil::makeSSLContext(std::vector<std::shared_ptr<X509>>(), nullptr, true), true);
#endif  // defined(ENABLE_OPENSSL)
}

SSLInitor::~SSLInitor() {
#if defined(ENABLE_OPENSSL)
    EVP_cleanup();
    ERR_free_strings();
    ERR_clear_error();
    CRYPTO_set_locking_callback(nullptr);
    CRYPTO_cleanup_all_ex_data();
    CONF_modules_unload(1);
    CONF_modules_free();
#endif  // defined(ENABLE_OPENSSL)
}

bool SSLInitor::loadCertificate(const std::string& pem_or_p12, bool server_mode,
                     const std::string& password, bool is_file, bool is_default) {
    auto cers = SSLUtil::loadPublicKey(pem_or_p12, password, is_file);
    auto key = SSLUtil::loadPrivateKey(pem_or_p12, password, is_file);
    auto ssl_ctx = SSLUtil::makeSSLContext(cers, key, server_mode, true);
    if (!ssl_ctx) {
        return false;
    }
    for (auto& cer : cers) {
        auto server_name = SSLUtil::getServerName(cer.get());
        setContext(server_name, ssl_ctx, server_mode, is_default);
        break;
    }
    return true;
}

void SSLInitor::ignoreInvalidCertificate(bool ignore) {
    s_ignore_invalid_cer = ignore;
}

bool SSLInitor::trustCertificate(X509* cer, bool server_mode) {
    return SSLUtil::trustCertificate(ctx_empty_[server_mode].get(), cer);
}

bool SSLInitor::trustCertificate(const std::string& pem_p12_cer, bool server_mode,
                                 const std::string& password, bool is_file) {
    auto cers = SSLUtil::loadPublicKey(pem_p12_cer, password, is_file);
    for (auto& cer : cers) {
        trustCertificate(cer.get(), server_mode);
    }
    return true;
}

std::shared_ptr<SSL_CTX> SSLInitor::getSSLCtx(const std::string& vhost, bool server_mode) {
    auto ret = getSSLCtx_l(vhost, server_mode);
    if (ret) {
        return ret;
    }
    return getSSLCtxWildcards(vhost, server_mode);
}

std::shared_ptr<SSL_CTX> SSLInitor::getSSLCtx_l(const std::string& vhost_in, bool server_mode) {
    auto vhost = vhost_in;
    if (vhost.empty()) {
        if (!default_vhost_[server_mode].empty()) {
            vhost = default_vhost_[server_mode];
        } else {
            if (server_mode) {
                WarnL << "Server with ssl must have certification and key";
            }
            return ctx_empty_[server_mode];
        }
    }
    auto it = ctxs_[server_mode].find(vhost);
    if (it == ctxs_[server_mode].end()) {
        return nullptr;
    }
    return it->second;
}

std::shared_ptr<SSL> SSLInitor::makeSSL(bool server_mode) {
#if defined(ENABLE_OPENSSL)
    return SSLUtil::makeSSL(ctx_empty_[server_mode].get());
#else
    return nullptr;
#endif
}

bool SSLInitor::setContext(const std::string& vhost, const std::shared_ptr<SSL_CTX>& ctx,
                           bool server_mode, bool is_default) {
    if (!ctx) {
        return false;
    }
    setupCtx(ctx.get());
#if defined(ENABLE_OPENSSL)
    if (vhost.empty() && server_mode) {
        ctx_empty_[server_mode] = ctx;
        SSL_CTX_set_tlsext_servername_callback(ctx.get(), findCertificate);
        SSL_CTX_set_tlsext_servername_arg(ctx.get(), (void*)server_mode);
    } else {
        ctxs_[server_mode][vhost] = ctx;
        if (is_default) {
            default_vhost_[server_mode] = vhost;
        }
        if (vhost.find("**.") == 0) {
            ctxs_wildcards_[server_mode][vhost.substr(2)] = ctx;
        }
        DebugL << "Add certificate of: " << vhost;
    }
    return true;
#else
    WarnL << "ENABLE_OPENSSL disabled, you can not use any features based on openssl";
    return false;
#endif
}

void SSLInitor::setupCtx(SSL_CTX* ctx) {
#if defined(ENABLE_OPENSSL)
    SSLUtil::loadDefaultCAs(ctx);
    SSL_CTX_set_cipher_list(ctx, "ALL:!ADH:!LOW:!EXP:!MD5:!3DES:!DES:!IDEA:!RC4:@STRENGTH");  // 设置加密套件
    SSL_CTX_set_verify_depth(ctx, 9);  // 设置证书验证深度
    SSL_CTX_set_mode(ctx, SSL_MODE_AUTO_RETRY);  // 设置SSL模式
    SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_OFF);  // 设置会话缓存模式
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, [](int ok, X509_STORE_CTX* pStore) {
        if (!ok) {
            int depth = X509_STORE_CTX_get_error_depth(pStore);
            int err = X509_STORE_CTX_get_error(pStore);
            WarnL << "SSL_CTX_set_verify callback, depth: " << depth
                  << ", err: " << X509_verify_cert_error_string(err);
        }
        return s_ignore_invalid_cer ? 1 : ok;
    });
    unsigned long ssloptions = SSL_OP_ALL | SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION | SSL_OP_NO_COMPRESSION;
    // 禁用不安全的协议
#ifdef SSL_OP_NO_RENEGOTIATION
    ssloptions |= SSL_OP_NO_RENEGOTIATION;
#endif

#ifdef SSL_OP_NO_SSLv2
    ssloptions |= SSL_OP_NO_SSLv2;
#endif

#ifdef SSL_OP_NO_SSLv3
    ssloptions |= SSL_OP_NO_SSLv3;
#endif

#ifdef SSL_OP_NO_TLSv1
    ssloptions |= SSL_OP_NO_TLSv1;
#endif

#ifdef SSL_OP_NO_TLSv1_1
    ssloptions |= SSL_OP_NO_TLSv1_1;
#endif

    SSL_CTX_set_options(ctx, ssloptions);
#endif  // defined(ENABLE_OPENSSL)
}

std::shared_ptr<SSL_CTX> SSLInitor::getSSLCtxWildcards(const std::string& vhost, bool server_mode) {
    for (auto& pr : ctxs_wildcards_[server_mode]) {
        auto pos = strcasestr(vhost.data(), pr.first.data());
        if (pos && pos + pr.first.size() == &vhost.back() + 1) {
            return pr.second;
        }
    }
    return nullptr;
}

std::string SSLInitor::defaultVhost(bool server_mode) {
    return default_vhost_[server_mode];
}

int SSLInitor::findCertificate(SSL* ssl, int* ad, void* arg) {
#if !defined(ENABLE_OPENSSL)
    return 0;
#else
    if (!ssl) {
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    }
    SSL_CTX* ctx = nullptr;
    static auto& ref = SSLInitor::Instance();
    const char* vhost = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);

    if (vhost && vhost[0] != '\0') {
        ctx = ref.getSSLCtx_l(vhost, (bool)(arg)).get();
        if (!ctx) {
            WarnL << "Can not find any certificate of host: " << vhost
                  << ", select default certificate of: " << ref.default_vhost_[(bool)(arg)];
        }
    }
    if (!ctx) {
        ctx = ref.getSSLCtx("", (bool)(arg)).get();
    }
    if (!ctx) {
        WarnL << "Can not find any available certificate of host: "
              << (vhost ? vhost : "default host") << ", tls handshake failed";
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    }
    SSL_set_SSL_CTX(ssl, ctx);
    return SSL_TLSEXT_ERR_OK;
#endif
}

///////////////////////////// SSLBox /////////////////////////////


SSLBox::SSLBox(bool server_mode, bool enable, int buff_size) {
#if defined(ENABLE_OPENSSL)
    read_bio_ = BIO_new(BIO_s_mem());
    server_mode_ = server_mode;
    if (enable) {
        ssl_ = SSLInitor::Instance().makeSSL(server_mode);
    }
    if (ssl_) {
        write_bio_ = BIO_new(BIO_s_mem());
        SSL_set_bio(ssl_.get(), read_bio_, write_bio_);  // 设置SSL的读写缓冲区
        server_mode_ ? SSL_set_accept_state(ssl_.get())
                     : SSL_set_connect_state(ssl_.get());
    } else {
        WarnL << "makeSSL failed";
    }
    send_handshake_ = false;
    buff_size_ = buff_size;
#endif
}

SSLBox::~SSLBox() {}

void SSLBox::onRecv(const Buffer::Ptr& buffer) {
    if (!buffer->size()) {
        return ;
    }
    // ssl_为空，则不进行加密/解密
    if (!ssl_) {
        if (on_dec_) {
            on_dec_(buffer);
        }
        return ;
    }
#if defined(ENABLE_OPENSSL)
    uint32_t offset = 0;
    while (offset < buffer->size()) {
        auto nwrite = BIO_write(read_bio_, buffer->data() + offset, buffer->size() - offset);
        if (nwrite > 0) {
            offset += nwrite;
            flush();
            continue;
        }
        ErrorL << "Ssl error on BIO_write: " << SSLUtil::getLastError();
        shutdown();
        break;
    }
#endif
}

void SSLBox::onSend(Buffer::Ptr buffer) {
    if (!buffer->size()) {
        return ;
    }

    if (!ssl_) {
        if (on_enc_) {
            on_enc_(buffer);
        }
        return ;
    }
#if defined(ENABLE_OPENSSL)
    if (!server_mode_ && !send_handshake_) {
        send_handshake_ = true;
        SSL_do_handshake(ssl_.get());
    }
    buffer_send_.emplace_back(std::move(buffer));
    flush();
#endif
}

void SSLBox::setOnDecData(const std::function<void(const Buffer::Ptr&)>& cb) { on_dec_ = cb; }

void SSLBox::setOnEncData(const std::function<void(const Buffer::Ptr&)>& cb) { on_enc_ = cb; }

void SSLBox::shutdown() {
#if defined(ENABLE_OPENSSL)
    buffer_send_.clear();
    int ret = SSL_shutdown(ssl_.get());
    if (ret != 1) {
        ErrorL << "SSL_shutdown failed: " << SSLUtil::getLastError();
    } else {
        flush();
    }
#endif
}

void SSLBox::flush() {
#if defined(ENABLE_OPENSSL)
    if (is_flush_) {
        return ;
    }
    onceToken token([&]() { is_flush_ = true; }, [&]() { is_flush_ = false; });
    flushReadBio();
    if (!SSL_is_init_finished(ssl_.get()) || buffer_send_.empty()) {
        flushWriteBio();
        return ;
    }

    while (!buffer_send_.empty()) {
        auto& front = buffer_send_.front();
        uint32_t offset = 0;
        while (offset < front->size()) {
            // 将buffer_send_中的数据加密后写入到与ssl_关联的write_bio_中
            auto nwrite = SSL_write(ssl_.get(), front->data() + offset, front->size() - offset);
            if (nwrite > 0) {
                offset += nwrite;
                flushWriteBio();
                continue;
            }
            break;
        }
        if (offset != front->size()) {
            ErrorL << "Ssl error on SSL_write: " << SSLUtil::getLastError();
            shutdown();
            break;
        }
        buffer_send_.pop_front();
    }
#endif  // defined(ENABLE_OPENSSL)
}

void SSLBox::flushWriteBio() {
#if defined(ENABLE_OPENSSL)
    int total = 0;  // 累计读取的数据长度
    int nread = 0;  // 一次读取的数据长度
    auto buffer_bio = buffer_pool_.obtain2();
    buffer_bio->setCapacity(buff_size_);
    auto buf_size = buffer_bio->getCapacity() - 1;
    do {
        // 从write_bio_中读取加密后的数据到buffer_bio, 准备发送
        nread = BIO_read(write_bio_, buffer_bio->data() + total, buf_size - total);  
        if (nread > 0) {
            total += nread;
        }
    } while (nread > 0 && buf_size - total > 0);

    if (!total) {
        return ;
    }
    buffer_bio->data()[total] = '\0';
    buffer_bio->setSize(total);
    if (on_enc_) {
        on_enc_(buffer_bio);
    }

    if (nread > 0) {
        flushWriteBio();
    }
#endif  // defined(ENABLE_OPENSSL)
}

void SSLBox::flushReadBio() {
#if defined(ENABLE_OPENSSL)
    int total = 0;
    int nread = 0;
    auto buffer_bio = buffer_pool_.obtain2();
    buffer_bio->setCapacity(buff_size_);
    auto buf_size = buffer_bio->getCapacity() - 1;
    do {
        // 将与ssl_关联的read_bio_接收缓冲区中的加密数据进行解密，写入到buffer_bio中
        nread = SSL_read(ssl_.get(), buffer_bio->data() + total, buf_size - total);
        if (nread > 0) {
            total += nread;
        }
    } while (nread > 0 && buf_size - total > 0);

    if (!total) {
        return ;
    }

    buffer_bio->data()[total] = '\0';
    buffer_bio->setSize(total);
    if (on_dec_) {
        on_dec_(buffer_bio);  // 使用on_dec_回调对数据进行解密
    }
    
    if (nread > 0) {
        flushReadBio();
    }
#endif  // defined(ENABLE_OPENSSL)
}

bool SSLBox::setHost(const char* host) {
    if (!ssl_) {
        return false;
    }
    return 0 != SSL_set_tlsext_host_name(ssl_.get(), host);
}
}  // namespace xkernel



// 默认openssl支持sni

