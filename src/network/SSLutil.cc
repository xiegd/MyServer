#include "SSLutil.h"

#include <vector>
#include <string>
#include <memory>

#include "logger.h"
#include "utility.h"


#define ENABLE_OPENSSL 1

#if defined(ENABLE_OPENSSL)
#include <openssl/bio.h>
#include <openssl/conf.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/ossl_typ.h>
#include <openssl/pkcs12.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#endif  // defined(ENABLE_OPENSSL)

namespace xkernel {
    
std::string SSLUtil::getLastError() {
#if defined(ENABLE_OPENSSL)
    unsigned long errCode = ERR_get_error();  // 从错误队列中获取错误码
    if (errCode != 0) {
        char buffer[256];
        ERR_error_string_n(errCode, buffer, sizeof(buffer));  // 将错误码转换为可读的错误信息字符串
        return buffer;
    }
#endif  // defined(ENABLE_OPENSSL)
    {
        return "No error";
    }
}

#if defined(ENABLE_OPENSSL)
static int getCerType(BIO* bio, const char* passwd, X509** x509, int type) {
    // 尝试pem格式
    if (type == 1 || type == 0) {
        if (type == 0) {
            BIO_reset(bio);
        }
        // 尝试PEM格式
        *x509 = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
        if (*x509) {
            return 1;
        }
    }

    if (type == 2 || type == 0) {
        if (type == 0) {
            BIO_reset(bio);
        }
        // 尝试DER格式
        *x509 = d2i_X509_bio(bio, nullptr);
        if (*x509) {
            return 2;
        }
    }

    if (type == 3 || type == 0) {
        if (type == 0) {
            BIO_reset(bio);
        }
        // 尝试p12格式
        PKCS12* p12 = d2i_PKCS12_bio(bio, nullptr);
        if (p12) {
            EVP_PKEY* pkey = nullptr;
            PKCS12_parse(p12, passwd, &pkey, x509, nullptr);
            PKCS12_free(p12);
            if (pkey) {
                EVP_PKEY_free(pkey);
            }
            if (*x509) {
                return 3;
            }
        }
    }

    return 0;
}
#endif

std::vector<SSLUtil::X509_Ptr> SSLUtil::loadPublicKey(const std::string& path_or_data,
    const std::string& passwd, bool is_file) {
    std::vector<SSLUtil::X509_Ptr> ret;
#if defined(ENABLE_OPENSSL)
    BIO* bio = is_file ? BIO_new_file(const_cast<char*>(path_or_data.data()), "r")
                       : BIO_new_mem_buf(const_cast<char*>(path_or_data.data()), path_or_data.size());
    if (!bio) {
        WarnL << (is_file ? "BIO_new_file" : "BIO_new_mem_buf") << "failed: " << getLastError();
        return ret;
    }

    onceToken token0(nullptr, [&]() { BIO_free(bio); });  // 析构时释放BIO对象

    int cer_type = 0;
    X509* x509 = nullptr;
    do {
        cer_type = getCerType(bio, passwd.data(), &x509, cer_type);
        if (cer_type) {
            ret.push_back(X509_Ptr(x509, [](X509* ptr) { X509_free(ptr); }));
        }
    } while (cer_type != 0);

    return ret;
#else 
    return ret;
#endif  // defined(ENABLE_OPENSSL)
}

SSLUtil::EVP_PKEY_Ptr SSLUtil::loadPrivateKey(const std::string& path_or_data,
                                              const std::string& passwd, bool is_file) {
#if defined(ENABLE_OPENSSL)
    BIO* bio = is_file ? BIO_new_file(const_cast<char*>(path_or_data.data()), "r")
                       : BIO_new_mem_buf(const_cast<char*>(path_or_data.data()), path_or_data.size());
    
    if (!bio) {
        WarnL << (is_file ? "BIO_new_file" : "BIO_new_mem_buf") << "failed: " << getLastError();
        return nullptr;
    }
    // 密码回调函数，用于将用户数据中的密码复制到buf中
    pem_password_cb* cb = [](char* buf, int size, int rwflag, void* userdata) -> int {
        const std::string* passwd = reinterpret_cast<const std::string*>(userdata);
        size = size < passwd->size() ? size : passwd->size();
        memcpy(buf, passwd->data(), size);
        return size;
    };

    onceToken token0(nullptr, [&]() { BIO_free(bio); });
    // 尝试pem格式
    EVP_PKEY* evp_key = PEM_read_bio_PrivateKey(bio, nullptr, cb, (void*)&passwd);
    if (!evp_key) {
        BIO_reset(bio);  // 重置BIO对象
        PKCS12* p12 = d2i_PKCS12_bio(bio, nullptr);
        if (!p12) {
            return nullptr;
        }
        X509* x509 = nullptr;
        PKCS12_parse(p12, passwd.data(), &evp_key, &x509, nullptr);  // 解析PKCS12结构体，提取私钥和证书
        PKCS12_free(p12);
        if (x509) {
            X509_free(x509);
        }
        if (!evp_key) {
            return nullptr;
        }
    }
    return std::shared_ptr<EVP_PKEY>(evp_key, [](EVP_PKEY* ptr) { EVP_PKEY_free(ptr); });
#else
    return nullptr;
#endif
}

SSLUtil::SSL_CTX_Ptr SSLUtil::makeSSLContext(const std::vector<X509_Ptr>& cers, 
    const EVP_PKEY_Ptr& key, bool server_mode, bool check_key) {
#if defined(ENABLE_OPENSSL)
    SSL_CTX* ctx = SSL_CTX_new(server_mode ? SSLv23_server_method() : SSLv23_client_method());
    if (!ctx) {
        WarnL << "SSL_CTX_new" << (server_mode ? "SSLv23_server_method" : "SSLv23_client_method") 
              << "failed: " << getLastError();
        return nullptr;
    }
    int i = 0;
    // 加载证书
    for (auto& cer : cers) {
        if (i++ == 0) {
            SSL_CTX_use_certificate(ctx, cer.get());
        } else {
            SSL_CTX_add_extra_chain_cert(ctx, X509_dup(cer.get()));
        }
    }
    // 加载私钥
    if (key) {
        if (SSL_CTX_use_PrivateKey(ctx, key.get()) != 1) {
            WarnL << "SSL_CTX_use_PrivateKey failed: " << getLastError();
            SSL_CTX_free(ctx);
            return nullptr;
        }
    }
    // 验证私钥和证书是否匹配
    if (key || check_key) {
        if (SSL_CTX_check_private_key(ctx) != 1) {
            WarnL << "SSL_CTX_check_private_key failed: " << getLastError();
            SSL_CTX_free(ctx);
            return nullptr;
        }
    }
    return std::shared_ptr<SSL_CTX>(ctx, [](SSL_CTX* ptr) { SSL_CTX_free(ptr); });
#else 
    return nullptr;
#endif  // defined(ENABLE_OPENSSL)
}

SSLUtil::SSL_Ptr SSLUtil::makeSSL(SSL_CTX* ctx) {
#if defined(ENABLE_OPENSSL)
    auto* ssl = SSL_new(ctx);
    if (!ssl) {
        return nullptr;
    }
    return std::shared_ptr<SSL>(ssl, [](SSL* ptr) { SSL_free(ptr); });
#else 
    return nullptr;
#endif
}

bool SSLUtil::loadDefaultCAs(SSL_CTX* ctx) {
#if defined(ENABLE_OPENSSL)
    if (!ctx) {
        return false;
    }
    // 设置ctx的系统默认的证书验证路径, 其中包含了CA列表
    if (SSL_CTX_set_default_verify_paths(ctx) != 1) {
        WarnL << "SSL_CTX_set_default_verify_paths failed: " << getLastError();
        return false;
    }
    return true;
#else
    return false;
#endif
}

bool SSLUtil::trustCertificate(SSL_CTX* ctx, X509* cer) {
#if defined(ENABLE_OPENSSL)
    X509_STORE* store = SSL_CTX_get_cert_store(ctx);
    if (store && cer) {
        if (X509_STORE_add_cert(store, cer) != 1) {
            WarnL << "X509_STORE_add_cert failed: " << getLastError();
            return false;
        }
        return true;
    }
#endif
    return false;
}

bool SSLUtil::verifyX509(X509* cer, ...) {
#if defined(ENABLE_OPENSSL)
    va_list args;
    va_start(args, cer);
    X509_STORE* store = X509_STORE_new();
    do {
        X509* ca;
        if ((ca = va_arg(args, X509*)) == nullptr) {
            break;
        }
        X509_STORE_add_cert(store, ca);
    } while (true);
    va_end(args);

    X509_STORE_CTX* store_ctx = X509_STORE_CTX_new();
    X509_STORE_CTX_init(store_ctx, store, cer, nullptr);
    auto ret = X509_verify_cert(store_ctx);  // 验证证书是否可信
    if (ret != 1) {
        int depth = X509_STORE_CTX_get_error_depth(store_ctx);  // 获取错误深度
        int err = X509_STORE_CTX_get_error(store_ctx);
        WarnL << "X509_verify_cert failed, depth: " << depth << ", err: " << X509_verify_cert_error_string(err);
    }
    // 释放X509_STORE_CTX对象和X509_STORE对象
    X509_STORE_CTX_free(store_ctx);
    X509_STORE_free(store);
    return ret == 1;
#else
    WarnL << "ENABLE_OPENSSL disabled, you can not use any features based on openssl";
    return false;
#endif
}

// 使用公钥进行加密/解密
std::string SSLUtil::cryptWithRsaPublicKey(X509* cer, const std::string& in_str, bool enc_or_dec) {
#if defined(ENABLE_OPENSSL)
    EVP_PKEY* public_key = X509_get0_pubkey(cer);
    if (!public_key) {
        return "";
    }
    auto rsa = EVP_PKEY_get1_RSA(public_key);
    if (!rsa) {
        return "";
    }
    std::string out_str(RSA_size(rsa), '\0');
    int ret = 0;
    if (enc_or_dec) {
        ret = RSA_public_encrypt(in_str.size(), (uint8_t*)in_str.data(), 
                                 (uint8_t*)out_str.data(), rsa, RSA_PKCS1_PADDING);
    } else {
        ret = RSA_public_decrypt(in_str.size(), (uint8_t*)in_str.data(), 
                                 (uint8_t*)out_str.data(), rsa, RSA_PKCS1_PADDING);
    }
    if (ret > 0) {
        out_str.resize(ret);
        return out_str;
    }
    WarnL << (enc_or_dec ? "RSA_public_encrypt" : "RSA_public_decrypt") << "failed: " << getLastError();
    return "";
#else
    WarnL << "ENABLE_OPENSSL disabled, you can not use any features based on openssl";
    return "";
#endif
}

// 使用私钥进行加密/解密
std::string SSLUtil::cryptWithRsaPrivateKey(EVP_PKEY* private_key, const std::string& in_str, bool enc_or_dec) {
#if defined(ENABLE_OPENSSL)
    auto rsa = EVP_PKEY_get1_RSA(private_key);
    if (!rsa) {
        return "";
    }
    std::string out_str(RSA_size(rsa), '\0');  // 初始化输出字符串
    int ret = 0;
    if (enc_or_dec) {
        ret = RSA_private_encrypt(in_str.size(), (uint8_t*)in_str.data(),
                                  (uint8_t*)out_str.data(), rsa, RSA_PKCS1_PADDING);
    } else {
        ret = RSA_private_decrypt(in_str.size(), (uint8_t*)in_str.data(),
                                  (uint8_t*)out_str.data(), rsa, RSA_PKCS1_PADDING);
    }
    if (ret > 0) {
        out_str.resize(ret);
        return out_str;
    }
    WarnL << getLastError();
    return "";
#else
    WarnL << "ENABLE_OPENSSL disabled, you can not use any features based on openssl";
    return "";
#endif
}

std::string SSLUtil::getServerName(X509* cer) {
#if defined(ENABLE_OPENSSL) && defined(SSL_CTRL_SET_TLSEXT_HOSTNAME)
    if (!cer) {
        return "";
    }
    X509_NAME* name = X509_get_subject_name(cer);
    char ret[256] = {0};
    X509_NAME_get_text_by_NID(name, NID_commonName, ret, sizeof(ret));
    return ret;
#else
    return "";
#endif
}

}  
