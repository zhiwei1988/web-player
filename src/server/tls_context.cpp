#include "tls_context.h"

#include <mbedtls/error.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/x509_csr.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>

#include <cstdio>
#include <cstring>
#include <ctime>

namespace server {

static const char* PERSONALIZATION = "video_server_tls";

TlsContext::TlsContext() : initialized_(false) {
    mbedtls_ssl_config_init(&sslConfig_);
    mbedtls_ctr_drbg_init(&ctrDrbg_);
    mbedtls_entropy_init(&entropy_);
    mbedtls_x509_crt_init(&cert_);
    mbedtls_pk_init(&pkey_);
}

TlsContext::~TlsContext() {
    mbedtls_pk_free(&pkey_);
    mbedtls_x509_crt_free(&cert_);
    mbedtls_entropy_free(&entropy_);
    mbedtls_ctr_drbg_free(&ctrDrbg_);
    mbedtls_ssl_config_free(&sslConfig_);
}

bool TlsContext::Initialize(const std::string& certPath, const std::string& keyPath) {
    if (initialized_) {
        return true;
    }

    int ret = mbedtls_ctr_drbg_seed(&ctrDrbg_, mbedtls_entropy_func, &entropy_,
                                     reinterpret_cast<const unsigned char*>(PERSONALIZATION),
                                     std::strlen(PERSONALIZATION));
    if (ret != 0) {
        char errBuf[256];
        mbedtls_strerror(ret, errBuf, sizeof(errBuf));
        std::fprintf(stderr, "mbedtls_ctr_drbg_seed failed: %s\n", errBuf);
        return false;
    }

    if (!certPath.empty() && !keyPath.empty()) {
        if (!LoadCertificate(certPath, keyPath)) {
            return false;
        }
    } else {
        if (!GenerateSelfSignedCert()) {
            return false;
        }
    }

    ret = mbedtls_ssl_config_defaults(&sslConfig_,
                                       MBEDTLS_SSL_IS_SERVER,
                                       MBEDTLS_SSL_TRANSPORT_STREAM,
                                       MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        char errBuf[256];
        mbedtls_strerror(ret, errBuf, sizeof(errBuf));
        std::fprintf(stderr, "mbedtls_ssl_config_defaults failed: %s\n", errBuf);
        return false;
    }

    mbedtls_ssl_conf_rng(&sslConfig_, mbedtls_ctr_drbg_random, &ctrDrbg_);
    mbedtls_ssl_conf_ca_chain(&sslConfig_, cert_.next, nullptr);

    ret = mbedtls_ssl_conf_own_cert(&sslConfig_, &cert_, &pkey_);
    if (ret != 0) {
        char errBuf[256];
        mbedtls_strerror(ret, errBuf, sizeof(errBuf));
        std::fprintf(stderr, "mbedtls_ssl_conf_own_cert failed: %s\n", errBuf);
        return false;
    }

    initialized_ = true;
    return true;
}

bool TlsContext::LoadCertificate(const std::string& certPath, const std::string& keyPath) {
    int ret = mbedtls_x509_crt_parse_file(&cert_, certPath.c_str());
    if (ret != 0) {
        char errBuf[256];
        mbedtls_strerror(ret, errBuf, sizeof(errBuf));
        std::fprintf(stderr, "Failed to load certificate from %s: %s\n", certPath.c_str(), errBuf);
        return false;
    }

    ret = mbedtls_pk_parse_keyfile(&pkey_, keyPath.c_str(), nullptr);
    if (ret != 0) {
        char errBuf[256];
        mbedtls_strerror(ret, errBuf, sizeof(errBuf));
        std::fprintf(stderr, "Failed to load private key from %s: %s\n", keyPath.c_str(), errBuf);
        return false;
    }

    std::printf("Loaded certificate from: %s\n", certPath.c_str());
    std::printf("Loaded private key from: %s\n", keyPath.c_str());
    return true;
}

bool TlsContext::GenerateSelfSignedCert() {
    std::printf("Generating self-signed certificate (RSA 2048)...\n");

    int ret = mbedtls_pk_setup(&pkey_, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
    if (ret != 0) {
        char errBuf[256];
        mbedtls_strerror(ret, errBuf, sizeof(errBuf));
        std::fprintf(stderr, "mbedtls_pk_setup failed: %s\n", errBuf);
        return false;
    }

    ret = mbedtls_rsa_gen_key(mbedtls_pk_rsa(pkey_), mbedtls_ctr_drbg_random, &ctrDrbg_, 2048, 65537);
    if (ret != 0) {
        char errBuf[256];
        mbedtls_strerror(ret, errBuf, sizeof(errBuf));
        std::fprintf(stderr, "mbedtls_rsa_gen_key failed: %s\n", errBuf);
        return false;
    }

    mbedtls_x509write_cert writeCert;
    mbedtls_x509write_crt_init(&writeCert);
    mbedtls_x509write_crt_set_version(&writeCert, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&writeCert, MBEDTLS_MD_SHA256);

    ret = mbedtls_x509write_crt_set_subject_name(&writeCert, "CN=localhost,O=VideoServer,C=CN");
    if (ret != 0) {
        char errBuf[256];
        mbedtls_strerror(ret, errBuf, sizeof(errBuf));
        std::fprintf(stderr, "mbedtls_x509write_crt_set_subject_name failed: %s\n", errBuf);
        mbedtls_x509write_crt_free(&writeCert);
        return false;
    }

    ret = mbedtls_x509write_crt_set_issuer_name(&writeCert, "CN=localhost,O=VideoServer,C=CN");
    if (ret != 0) {
        char errBuf[256];
        mbedtls_strerror(ret, errBuf, sizeof(errBuf));
        std::fprintf(stderr, "mbedtls_x509write_crt_set_issuer_name failed: %s\n", errBuf);
        mbedtls_x509write_crt_free(&writeCert);
        return false;
    }

    mbedtls_x509write_crt_set_subject_key(&writeCert, &pkey_);
    mbedtls_x509write_crt_set_issuer_key(&writeCert, &pkey_);

    char serialNum[32] = "1";
    mbedtls_mpi serial;
    mbedtls_mpi_init(&serial);
    mbedtls_mpi_read_string(&serial, 10, serialNum);
    mbedtls_x509write_crt_set_serial(&writeCert, &serial);
    mbedtls_mpi_free(&serial);

    char notBefore[16];
    char notAfter[16];
    std::time_t now = std::time(nullptr);
    std::strftime(notBefore, sizeof(notBefore), "%Y%m%d%H%M%S", std::gmtime(&now));
    std::time_t expiry = now + 365 * 24 * 3600;
    std::strftime(notAfter, sizeof(notAfter), "%Y%m%d%H%M%S", std::gmtime(&expiry));

    mbedtls_x509write_crt_set_validity(&writeCert, notBefore, notAfter);

    unsigned char certBuf[4096];
    ret = mbedtls_x509write_crt_pem(&writeCert, certBuf, sizeof(certBuf),
                                     mbedtls_ctr_drbg_random, &ctrDrbg_);
    mbedtls_x509write_crt_free(&writeCert);

    if (ret != 0) {
        char errBuf[256];
        mbedtls_strerror(ret, errBuf, sizeof(errBuf));
        std::fprintf(stderr, "mbedtls_x509write_crt_pem failed: %s\n", errBuf);
        return false;
    }

    ret = mbedtls_x509_crt_parse(&cert_, certBuf, std::strlen(reinterpret_cast<char*>(certBuf)) + 1);
    if (ret != 0) {
        char errBuf[256];
        mbedtls_strerror(ret, errBuf, sizeof(errBuf));
        std::fprintf(stderr, "mbedtls_x509_crt_parse failed: %s\n", errBuf);
        return false;
    }

    std::printf("Self-signed certificate generated successfully\n");
    return true;
}

}  // namespace server
