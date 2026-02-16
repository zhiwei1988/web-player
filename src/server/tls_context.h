#ifndef TLS_CONTEXT_H
#define TLS_CONTEXT_H

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>

#include <string>

namespace server {

class TlsContext {
public:
    TlsContext();
    ~TlsContext();

    TlsContext(const TlsContext&) = delete;
    TlsContext& operator=(const TlsContext&) = delete;

    bool Initialize(const std::string& certPath, const std::string& keyPath);

    mbedtls_ssl_config* GetConfig() { return &sslConfig_; }

private:
    bool LoadCertificate(const std::string& certPath, const std::string& keyPath);
    bool GenerateSelfSignedCert();

    mbedtls_ssl_config sslConfig_;
    mbedtls_ctr_drbg_context ctrDrbg_;
    mbedtls_entropy_context entropy_;
    mbedtls_x509_crt cert_;
    mbedtls_pk_context pkey_;
    bool initialized_;
};

}  // namespace server

#endif  // TLS_CONTEXT_H
