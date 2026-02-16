#include "tls_server.h"

#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace server {

TlsServer::TlsServer() {
}

TlsServer::~TlsServer() {
    Stop();
}

bool TlsServer::Start(uint16_t port, const std::string& certPath, const std::string& keyPath) {
    if (!tlsContext_.Initialize(certPath, keyPath)) {
        return false;
    }

    TcpCallbacks tcpCallbacks;
    tcpCallbacks.onConnect = [this](int32_t fd, const std::string& ip) {
        OnTcpConnect(fd, ip);
    };
    tcpCallbacks.onDisconnect = [this](int32_t fd) {
        OnTcpDisconnect(fd);
    };
    tcpCallbacks.onData = [this](int32_t fd, const uint8_t* data, size_t len) {
        OnTcpData(fd, data, len);
    };

    tcpServer_.SetCallbacks(tcpCallbacks);

    return tcpServer_.Start(port);
}

void TlsServer::Stop() {
    for (auto& pair : tlsConnections_) {
        mbedtls_ssl_close_notify(&pair.second.ssl);
        mbedtls_ssl_free(&pair.second.ssl);
    }
    tlsConnections_.clear();
    tcpServer_.Stop();
}

void TlsServer::ProcessEvents(int32_t timeoutMs) {
    tcpServer_.ProcessEvents(timeoutMs);
}

void TlsServer::RegisterTimer(int32_t timerFd) {
    tcpServer_.RegisterTimer(timerFd);
}

void TlsServer::SetCallbacks(const TcpCallbacks& callbacks) {
    userCallbacks_ = callbacks;
}

void TlsServer::SetTimerCallback(std::function<void()> callback) {
    tcpServer_.SetTimerCallback(callback);
}

bool TlsServer::IsRunning() const {
    return tcpServer_.IsRunning();
}

void TlsServer::OnTcpConnect(int32_t fd, const std::string& ip) {
    if (!StartTlsHandshake(fd)) {
        tcpServer_.CloseConnection(fd);
        return;
    }
}

void TlsServer::OnTcpDisconnect(int32_t fd) {
    bool wasConnected = false;
    auto it = tlsConnections_.find(fd);
    if (it != tlsConnections_.end()) {
        wasConnected = it->second.handshakeComplete;
    }
    RemoveTlsConnection(fd);
    if (wasConnected && userCallbacks_.onDisconnect) {
        userCallbacks_.onDisconnect(fd);
    }
}

void TlsServer::OnTcpData(int32_t fd, const uint8_t* data, size_t len) {
    auto it = tlsConnections_.find(fd);
    if (it == tlsConnections_.end()) {
        return;
    }

    // Append raw TCP data to the TLS connection's receive buffer
    it->second.recvBuf.insert(it->second.recvBuf.end(), data, data + len);

    if (!it->second.handshakeComplete) {
        ContinueTlsHandshake(fd);
        return;
    }

    // Read decrypted data from mbedtls
    uint8_t buffer[65536];
    while (true) {
        int ret = mbedtls_ssl_read(&it->second.ssl, buffer, sizeof(buffer));
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            break;
        }
        if (ret <= 0) {
            if (ret != MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
                char errBuf[256];
                mbedtls_strerror(ret, errBuf, sizeof(errBuf));
                std::fprintf(stderr, "mbedtls_ssl_read failed: %s\n", errBuf);
            }
            tcpServer_.CloseConnection(fd);
            return;
        }

        if (userCallbacks_.onData) {
            userCallbacks_.onData(fd, buffer, static_cast<size_t>(ret));
        }
    }
}

bool TlsServer::StartTlsHandshake(int32_t fd) {
    TlsConnection tlsConn;
    mbedtls_ssl_init(&tlsConn.ssl);
    tlsConn.handshakeComplete = false;
    tlsConn.fd = fd;
    tlsConn.recvBufOffset = 0;

    int ret = mbedtls_ssl_setup(&tlsConn.ssl, tlsContext_.GetConfig());
    if (ret != 0) {
        char errBuf[256];
        mbedtls_strerror(ret, errBuf, sizeof(errBuf));
        std::fprintf(stderr, "mbedtls_ssl_setup failed: %s\n", errBuf);
        mbedtls_ssl_free(&tlsConn.ssl);
        return false;
    }

    tlsConnections_[fd] = std::move(tlsConn);

    // Set BIO after inserting into map, so the pointer is stable
    auto it = tlsConnections_.find(fd);
    mbedtls_ssl_set_bio(&it->second.ssl, &it->second, SslSend, SslRecv, nullptr);

    return true;
}

void TlsServer::ContinueTlsHandshake(int32_t fd) {
    auto it = tlsConnections_.find(fd);
    if (it == tlsConnections_.end()) {
        return;
    }

    int ret = mbedtls_ssl_handshake(&it->second.ssl);
    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
        return;
    }

    if (ret != 0) {
        char errBuf[256];
        mbedtls_strerror(ret, errBuf, sizeof(errBuf));
        std::fprintf(stderr, "TLS handshake failed: %s\n", errBuf);
        tcpServer_.CloseConnection(fd);
        return;
    }

    it->second.handshakeComplete = true;
    std::printf("TLS handshake completed for fd %d\n", fd);

    if (userCallbacks_.onConnect) {
        userCallbacks_.onConnect(fd, "");
    }
}

int32_t TlsServer::SendData(int32_t fd, const uint8_t* data, size_t len) {
    auto it = tlsConnections_.find(fd);
    if (it == tlsConnections_.end() || !it->second.handshakeComplete) {
        return -1;
    }

    size_t totalSent = 0;
    while (totalSent < len) {
        int ret = mbedtls_ssl_write(&it->second.ssl, data + totalSent, len - totalSent);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            continue;
        }
        if (ret < 0) {
            char errBuf[256];
            mbedtls_strerror(ret, errBuf, sizeof(errBuf));
            std::fprintf(stderr, "mbedtls_ssl_write failed: %s\n", errBuf);
            return -1;
        }
        totalSent += static_cast<size_t>(ret);
    }

    return static_cast<int32_t>(totalSent);
}

void TlsServer::CloseConnection(int32_t fd) {
    RemoveTlsConnection(fd);
    tcpServer_.CloseConnection(fd);
}

void TlsServer::RemoveTlsConnection(int32_t fd) {
    auto it = tlsConnections_.find(fd);
    if (it != tlsConnections_.end()) {
        mbedtls_ssl_close_notify(&it->second.ssl);
        mbedtls_ssl_free(&it->second.ssl);
        tlsConnections_.erase(it);
    }
}

int TlsServer::SslSend(void* ctx, const unsigned char* buf, size_t len) {
    TlsConnection* conn = static_cast<TlsConnection*>(ctx);
    ssize_t sent = send(conn->fd, buf, len, MSG_NOSIGNAL);
    if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return MBEDTLS_ERR_SSL_WANT_WRITE;
        }
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
    return static_cast<int>(sent);
}

int TlsServer::SslRecv(void* ctx, unsigned char* buf, size_t len) {
    TlsConnection* conn = static_cast<TlsConnection*>(ctx);
    size_t available = conn->recvBuf.size() - conn->recvBufOffset;
    if (available == 0) {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }

    size_t toRead = (len < available) ? len : available;
    std::memcpy(buf, conn->recvBuf.data() + conn->recvBufOffset, toRead);
    conn->recvBufOffset += toRead;

    // Compact buffer when fully consumed
    if (conn->recvBufOffset == conn->recvBuf.size()) {
        conn->recvBuf.clear();
        conn->recvBufOffset = 0;
    }

    return static_cast<int>(toRead);
}

}  // namespace server
