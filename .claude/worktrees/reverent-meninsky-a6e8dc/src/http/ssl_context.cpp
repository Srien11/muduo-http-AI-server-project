#include "http/ssl_context.h"

#include <iostream>

namespace muduo_http {

SslContext::SslContext() {
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
}

SslContext::~SslContext() {
    if (ctx_) {
        SSL_CTX_free(ctx_);
    }
}

bool SslContext::LoadCertificate(const SslConfig& config) {
    ctx_ = SSL_CTX_new(TLS_server_method());
    if (!ctx_) {
        std::cerr << "[ssl] failed to create SSL_CTX\n";
        return false;
    }

    SSL_CTX_set_min_proto_version(ctx_, TLS1_2_VERSION);

    if (SSL_CTX_use_certificate_file(ctx_, config.cert_file.c_str(), SSL_FILETYPE_PEM) <= 0) {
        std::cerr << "[ssl] failed to load cert: " << config.cert_file << '\n';
        ERR_print_errors_fp(stderr);
        return false;
    }

    if (SSL_CTX_use_PrivateKey_file(ctx_, config.key_file.c_str(), SSL_FILETYPE_PEM) <= 0) {
        std::cerr << "[ssl] failed to load key: " << config.key_file << '\n';
        ERR_print_errors_fp(stderr);
        return false;
    }

    if (!SSL_CTX_check_private_key(ctx_)) {
        std::cerr << "[ssl] cert and key do not match\n";
        return false;
    }

    std::cout << "[ssl] certificate loaded: " << config.cert_file << '\n';
    return true;
}

SSL* SslContext::CreateSsl(int fd) {
    if (!ctx_) return nullptr;
    SSL* ssl = SSL_new(ctx_);
    if (ssl) {
        SSL_set_fd(ssl, fd);
    }
    return ssl;
}

} // namespace muduo_http
