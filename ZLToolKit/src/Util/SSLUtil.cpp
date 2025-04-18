﻿/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "SSLUtil.h"

#include "logger.h"
#include "onceToken.h"

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

using namespace std;

namespace toolkit {

std::string SSLUtil::getLastError() {
#if defined(ENABLE_OPENSSL)
    unsigned long errCode = ERR_get_error();
    if (errCode != 0) {
        char buffer[256];
        ERR_error_string_n(errCode, buffer, sizeof(buffer));
        return buffer;
    } else
#endif  // defined(ENABLE_OPENSSL)
    {
        return "No error";
    }
}

#if defined(ENABLE_OPENSSL)

static int getCerType(BIO *bio, const char *passwd, X509 **x509, int type) {
    //尝试pem格式
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
        //尝试DER格式
        *x509 = d2i_X509_bio(bio, nullptr);
        if (*x509) {
            return 2;
        }
    }

    if (type == 3 || type == 0) {
        if (type == 0) {
            BIO_reset(bio);
        }
        //尝试p12格式
        PKCS12 *p12 = d2i_PKCS12_bio(bio, nullptr);
        if (p12) {
            EVP_PKEY *pkey = nullptr;
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

#endif  // defined(ENABLE_OPENSSL)

vector<shared_ptr<X509> > SSLUtil::loadPublicKey(
    const string &file_path_or_data, const string &passwd, bool isFile) {
    vector<shared_ptr<X509> > ret;
#if defined(ENABLE_OPENSSL)
    BIO *bio = isFile ? BIO_new_file((char *)file_path_or_data.data(), "r")
                      : BIO_new_mem_buf((char *)file_path_or_data.data(),
                                        file_path_or_data.size());
    if (!bio) {
        WarnL << (isFile ? "BIO_new_file" : "BIO_new_mem_buf")
              << " failed: " << getLastError();
        return ret;
    }

    onceToken token0(nullptr, [&]() { BIO_free(bio); });

    int cer_type = 0;
    X509 *x509 = nullptr;
    do {
        cer_type = getCerType(bio, passwd.data(), &x509, cer_type);
        if (cer_type) {
            ret.push_back(
                shared_ptr<X509>(x509, [](X509 *ptr) { X509_free(ptr); }));
        }
    } while (cer_type != 0);
    return ret;
#else
    return ret;
#endif  // defined(ENABLE_OPENSSL)
}

shared_ptr<EVP_PKEY> SSLUtil::loadPrivateKey(const string &file_path_or_data,
                                             const string &passwd,
                                             bool isFile) {
#if defined(ENABLE_OPENSSL)
    // 加载公钥， 根据文件路径或数据创建BIO对象
    // data 方法获取到的是const char*
    BIO *bio = isFile ? BIO_new_file((char *)file_path_or_data.data(), "r")
                      : BIO_new_mem_buf((char *)file_path_or_data.data(),
                                        file_path_or_data.size());
    if (!bio) {
        WarnL << (isFile ? "BIO_new_file" : "BIO_new_mem_buf")
              << " failed: " << getLastError();
        return nullptr;
    }
    // 密码回调函数, 用于将用户数据中的密码复制到buf中
    pem_password_cb *cb = [](char *buf, int size, int rwflag,
                             void *userdata) -> int {
        const string *passwd = (const string *)userdata;
        size = size < (int)passwd->size() ? size : (int)passwd->size();
        memcpy(buf, passwd->data(), size);
        return size;
    };
    // 使用onceToken管理BIO对象的生命周期, 析构时释放BIO对象
    onceToken token0(nullptr, [&]() { BIO_free(bio); });

    //尝试pem格式
    EVP_PKEY *evp_key =
        PEM_read_bio_PrivateKey(bio, nullptr, cb, (void *)&passwd);
    if (!evp_key) {
        //尝试p12格式
        BIO_reset(bio); // 重置BIO对象, 以便重新读取数据, 因为BIO是一个流对象
        PKCS12 *p12 = d2i_PKCS12_bio(bio, nullptr);
        if (!p12) {
            return nullptr;
        }
        X509 *x509 = nullptr;
        PKCS12_parse(p12, passwd.data(), &evp_key, &x509, nullptr);  // 解析PKCS12结构体，提取私钥和证书
        PKCS12_free(p12);
        if (x509) {
            X509_free(x509);  // 释放证书对象
        }
        if (!evp_key) {
            return nullptr;
        }
    }

    return shared_ptr<EVP_PKEY>(evp_key,
                                [](EVP_PKEY *ptr) { EVP_PKEY_free(ptr); });
#else
    return nullptr;
#endif  // defined(ENABLE_OPENSSL)
}

shared_ptr<SSL_CTX> SSLUtil::makeSSLContext(
    const vector<shared_ptr<X509> > &cers, const shared_ptr<EVP_PKEY> &key,
    bool serverMode, bool checkKey) {
#if defined(ENABLE_OPENSSL)
    SSL_CTX *ctx = SSL_CTX_new(serverMode ? SSLv23_server_method()
                                          : SSLv23_client_method());
    if (!ctx) {
        WarnL << "SSL_CTX_new "
              << (serverMode ? "SSLv23_server_method" : "SSLv23_client_method")
              << " failed: " << getLastError();
        return nullptr;
    }
    int i = 0;
    for (auto &cer : cers) {
        //加载公钥  
        if (i++ == 0) {
            // SSL_CTX_use_certificate内部会调用X509_up_ref,所以这里不用X509_dup
            SSL_CTX_use_certificate(ctx, cer.get());
        } else {
            //需要先拷贝X509对象，否则指针会失效  
            SSL_CTX_add_extra_chain_cert(ctx, X509_dup(cer.get()));
        }
    }

    if (key) {
        //提供了私钥  
        if (SSL_CTX_use_PrivateKey(ctx, key.get()) != 1) {
            WarnL << "SSL_CTX_use_PrivateKey failed: " << getLastError();
            SSL_CTX_free(ctx);
            return nullptr;
        }
    }

    if (key || checkKey) {
        // 验证私钥和证书是否匹配, 一台服务器可以多个证书以及与之匹配的私钥
        if (SSL_CTX_check_private_key(ctx) != 1) {
            WarnL << "SSL_CTX_check_private_key failed: " << getLastError();
            SSL_CTX_free(ctx);
            return nullptr;
        }
    }

    //公钥私钥匹配或者没有公私钥  
    return shared_ptr<SSL_CTX>(ctx, [](SSL_CTX *ptr) { SSL_CTX_free(ptr); });
#else
    return nullptr;
#endif  // defined(ENABLE_OPENSSL)
}

shared_ptr<SSL> SSLUtil::makeSSL(SSL_CTX *ctx) {
#if defined(ENABLE_OPENSSL)
    auto *ssl = SSL_new(ctx);
    if (!ssl) {
        return nullptr;
    }
    return shared_ptr<SSL>(ssl, [](SSL *ptr) { SSL_free(ptr); });
#else
    return nullptr;
#endif  // defined(ENABLE_OPENSSL)
}

bool SSLUtil::loadDefaultCAs(SSL_CTX *ctx) {
#if defined(ENABLE_OPENSSL)
    if (!ctx) {
        return false;
    }
    // 设置ctx的系统默认受信任证书颁发机构列表，用于验证对方证书, 设置默认验证路径成功返回true
    if (SSL_CTX_set_default_verify_paths(ctx) != 1) {
        WarnL << "SSL_CTX_set_default_verify_paths failed: " << getLastError();
        return false;
    }
    return true;
#else
    return false;
#endif  // defined(ENABLE_OPENSSL)
}

bool SSLUtil::trustCertificate(SSL_CTX *ctx, X509 *cer) {
#if defined(ENABLE_OPENSSL)
    X509_STORE *store = SSL_CTX_get_cert_store(ctx);  // 获取与ctx相关联的证书的结构体
    if (store && cer) {
        if (X509_STORE_add_cert(store, cer) != 1) {
            WarnL << "X509_STORE_add_cert failed: " << getLastError();
            return false;
        }
        return true;
    }
#endif  // defined(ENABLE_OPENSSL)
    return false;
}

// 验证给定的证书是否有效
bool SSLUtil::verifyX509(X509 *cer, ...) {
#if defined(ENABLE_OPENSSL)
    va_list args;  // 用于接收可变参数列表
    va_start(args, cer);  // 初始化可变参数列表
    X509_STORE *store = X509_STORE_new();
    do {
        X509 *ca;
        // 从可变参数列表中获取一个X509对象
        if ((ca = va_arg(args, X509 *)) == nullptr) {
            break;
        }
        X509_STORE_add_cert(store, ca);  // 将CA证书添加到store中
    } while (true);
    va_end(args);

    X509_STORE_CTX *store_ctx = X509_STORE_CTX_new();  // 创建一个X509_STORE_CTX对象
    X509_STORE_CTX_init(store_ctx, store, cer, nullptr);  // 初始化X509_STORE_CTX对象
    auto ret = X509_verify_cert(store_ctx);  // 验证证书是否可信
    if (ret != 1) {
        int depth = X509_STORE_CTX_get_error_depth(store_ctx);
        int err = X509_STORE_CTX_get_error(store_ctx);
        WarnL << "X509_verify_cert failed, depth: " << depth
              << ", err: " << X509_verify_cert_error_string(err);
    }
    // 释放X509_STORE_CTX对象和X509_STORE对象
    X509_STORE_CTX_free(store_ctx);
    X509_STORE_free(store);
    return ret == 1;
#else
    WarnL << "ENABLE_OPENSSL disabled, you can not use any features based on "
             "openssl";
    return false;
#endif  // defined(ENABLE_OPENSSL)
}

#ifdef ENABLE_OPENSSL
#ifndef X509_F_X509_PUBKEY_GET0
EVP_PKEY *X509_get0_pubkey(X509 *x) {
    EVP_PKEY *ret = X509_get_pubkey(x);
    if (ret) {
        EVP_PKEY_free(ret);
    }
    return ret;
}
#endif  // X509_F_X509_PUBKEY_GET0

#ifndef EVP_F_EVP_PKEY_GET0_RSA
RSA *EVP_PKEY_get0_RSA(EVP_PKEY *pkey) {
    RSA *ret = EVP_PKEY_get1_RSA(pkey);
    if (ret) {
        RSA_free(ret);
    }
    return ret;
}
#endif  // EVP_F_EVP_PKEY_GET0_RSA
#endif  // ENABLE_OPENSSL

// crypt: encryption or decryption， 加密或解密
// 下面两个方法分别实现公钥和私钥的加密/解密，非对称加密算法
// 因为公钥加密的只有私钥才能解密
// 使用公钥进行加密/解密， 只能使用私钥对加密后的数据进行解密
string SSLUtil::cryptWithRsaPublicKey(X509 *cer, const string &in_str,
                                      bool enc_or_dec) {
#if defined(ENABLE_OPENSSL)
    EVP_PKEY *public_key = X509_get0_pubkey(cer);
    if (!public_key) {
        return "";
    }
    auto rsa = EVP_PKEY_get1_RSA(public_key);
    if (!rsa) {
        return "";
    }
    string out_str(RSA_size(rsa), '\0');
    int ret = 0;
    if (enc_or_dec) {
        ret = RSA_public_encrypt(in_str.size(), (uint8_t *)in_str.data(),
                                 (uint8_t *)out_str.data(), rsa,
                                 RSA_PKCS1_PADDING);
    } else {
        ret = RSA_public_decrypt(in_str.size(), (uint8_t *)in_str.data(),
                                 (uint8_t *)out_str.data(), rsa,
                                 RSA_PKCS1_PADDING);
    }
    if (ret > 0) {
        out_str.resize(ret);
        return out_str;
    }
    WarnL << (enc_or_dec ? "RSA_public_encrypt" : "RSA_public_decrypt")
          << " failed: " << getLastError();
    return "";
#else
    WarnL << "ENABLE_OPENSSL disabled, you can not use any features based on "
             "openssl";
    return "";
#endif  // defined(ENABLE_OPENSSL)
}

// 使用私钥进行加密/解密
string SSLUtil::cryptWithRsaPrivateKey(EVP_PKEY *private_key,
                                       const string &in_str, bool enc_or_dec) {
#if defined(ENABLE_OPENSSL)
    auto rsa = EVP_PKEY_get1_RSA(private_key);
    if (!rsa) {
        return "";
    }
    string out_str(RSA_size(rsa), '\0');
    int ret = 0;
    if (enc_or_dec) {
        ret = RSA_private_encrypt(in_str.size(), (uint8_t *)in_str.data(),
                                  (uint8_t *)out_str.data(), rsa,
                                  RSA_PKCS1_PADDING);
    } else {
        ret = RSA_private_decrypt(in_str.size(), (uint8_t *)in_str.data(),
                                  (uint8_t *)out_str.data(), rsa,
                                  RSA_PKCS1_PADDING);
    }
    if (ret > 0) {
        out_str.resize(ret);
        return out_str;
    }
    WarnL << getLastError();
    return "";
#else
    WarnL << "ENABLE_OPENSSL disabled, you can not use any features based on "
             "openssl";
    return "";
#endif  // defined(ENABLE_OPENSSL)
}

string SSLUtil::getServerName(X509 *cer) {
#if defined(ENABLE_OPENSSL) && defined(SSL_CTRL_SET_TLSEXT_HOSTNAME)
    if (!cer) {
        return "";
    }
    //获取证书里的域名  
    X509_NAME *name = X509_get_subject_name(cer);
    char ret[256] = {0};
    X509_NAME_get_text_by_NID(name, NID_commonName, ret, sizeof(ret));
    return ret;
#else
    return "";
#endif
}

}  // namespace toolkit
