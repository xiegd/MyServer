#ifndef _SSLUTIL_H_
#define _SSLUTIL_H_

#include <memory>
#include <string>
#include <vector>

using X509 = struct x509_st;  // 数字证书
using EVP_PKEY = struct evp_pkey_st;  // 密钥
using SSL_CTX = struct ssl_ctx_st;  // SSL上下文
using SSL = struct ssl_st;  // SSL，表示一个SSL连接
using BIO = struct bio_st;  // BIO，I/O抽象层

namespace xkernel {

class SSLUtil {
public:
    using X509_Ptr = std::shared_ptr<X509>;
    using EVP_PKEY_Ptr = std::shared_ptr<EVP_PKEY>;
    using SSL_CTX_Ptr = std::shared_ptr<SSL_CTX>;
    using SSL_Ptr = std::shared_ptr<SSL>;

    static std::string getLastError();
    static std::vector<X509_Ptr> loadPublicKey(const std::string& file_path_or_data, 
                                               const std::string& passwd = "", bool is_file = true);
    static EVP_PKEY_Ptr loadPrivateKey(const std::string& file_path_or_data, 
                                       const std::string& passwd = "", bool is_file = true);
    static SSL_CTX_Ptr makeSSLContext(const std::vector<X509_Ptr>& cers, const EVP_PKEY_Ptr& key, 
                                      bool server_mode = true, bool check_key = false);
    static SSL_Ptr makeSSL(SSL_CTX* ctx);
    static bool loadDefaultCAs(SSL_CTX* ctx);
    static bool trustCertificate(SSL_CTX* ctx, X509* cer);
    static bool verifyX509(X509* cer, ...);  // 验证证书合法性
    static std::string cryptWithRsaPublicKey(X509* cer, const std::string& in_str, bool enc_or_dec);  // 使用公钥加解密数据
    static std::string cryptWithRsaPrivateKey(EVP_PKEY* private_key, const std::string& in_str, bool enc_or_dec);  // 使用私钥加解密数据
    static std::string getServerName(X509* cer);  // 获取证书域名
};

}  // namespace xkernel

#endif