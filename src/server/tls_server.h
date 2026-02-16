#ifndef TLS_SERVER_H
#define TLS_SERVER_H

#include <mbedtls/ssl.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "tcp_server.h"
#include "tls_context.h"

namespace server {

struct TlsConnection {
    mbedtls_ssl_context ssl;
    bool handshakeComplete;
    int32_t fd;
    std::vector<uint8_t> recvBuf;
    size_t recvBufOffset;
};

class TlsServer {
public:
    TlsServer();
    ~TlsServer();

    TlsServer(const TlsServer&) = delete;
    TlsServer& operator=(const TlsServer&) = delete;

    bool Start(uint16_t port, const std::string& certPath, const std::string& keyPath);

    void Stop();

    void ProcessEvents(int32_t timeoutMs);

    int32_t SendData(int32_t fd, const uint8_t* data, size_t len);

    void CloseConnection(int32_t fd);

    void RegisterTimer(int32_t timerFd);

    void SetCallbacks(const TcpCallbacks& callbacks);

    void SetTimerCallback(std::function<void()> callback);

    bool IsRunning() const;

private:
    void OnTcpConnect(int32_t fd, const std::string& ip);
    void OnTcpDisconnect(int32_t fd);
    void OnTcpData(int32_t fd, const uint8_t* data, size_t len);

    bool StartTlsHandshake(int32_t fd);
    void ContinueTlsHandshake(int32_t fd);
    void RemoveTlsConnection(int32_t fd);

    // mbedtls I/O callbacks: send directly to socket, recv from buffer
    static int SslSend(void* ctx, const unsigned char* buf, size_t len);
    static int SslRecv(void* ctx, unsigned char* buf, size_t len);

    TcpServer tcpServer_;
    TlsContext tlsContext_;
    std::unordered_map<int32_t, TlsConnection> tlsConnections_;
    TcpCallbacks userCallbacks_;
};

}  // namespace server

#endif  // TLS_SERVER_H
