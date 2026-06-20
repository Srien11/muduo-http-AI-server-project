#pragma once

#include <memory>
#include <string>

#include <openssl/ssl.h>
#include <openssl/err.h>

namespace muduo_http {

struct SslConfig {
    std::string cert_file;
    std::string key_file;
    std::string ca_file;   // optional
};

class SslContext {
public:
    SslContext();
    ~SslContext();

    bool LoadCertificate(const SslConfig& config);
    SSL* CreateSsl(int fd);

    SslContext(const SslContext&) = delete;
    SslContext& operator=(const SslContext&) = delete;

private:
    SSL_CTX* ctx_{nullptr};
};

} // namespace muduo_http
