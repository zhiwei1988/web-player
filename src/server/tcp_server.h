#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace server {

/**
 * @brief TCP connection event callbacks
 */
struct TcpCallbacks {
    std::function<void(int32_t fd, const std::string& ip)> onConnect;
    std::function<void(int32_t fd)> onDisconnect;
    std::function<void(int32_t fd, const uint8_t* data, size_t len)> onData;
};

/**
 * @brief TCP server using epoll for multiplexing
 */
class TcpServer {
public:
    TcpServer();
    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    /**
     * @brief Start listening on specified port
     * @param port TCP port number
     * @return true on success
     */
    bool Start(uint16_t port);

    /**
     * @brief Stop the server
     */
    void Stop();

    /**
     * @brief Process events with timeout
     * @param timeoutMs timeout in milliseconds (-1 for blocking)
     */
    void ProcessEvents(int32_t timeoutMs);

    /**
     * @brief Send data to a client
     * @param fd client file descriptor
     * @param data data buffer
     * @param len data length
     * @return bytes sent, -1 on error
     */
    int32_t SendData(int32_t fd, const uint8_t* data, size_t len);

    /**
     * @brief Close a client connection
     * @param fd client file descriptor
     */
    void CloseConnection(int32_t fd);

    /**
     * @brief Register a timer fd to epoll
     * @param timerFd timer file descriptor
     */
    void RegisterTimer(int32_t timerFd);

    /**
     * @brief Set event callbacks
     */
    void SetCallbacks(const TcpCallbacks& callbacks);

    /**
     * @brief Set timer callback
     */
    void SetTimerCallback(std::function<void()> callback);

    /**
     * @brief Check if server is running
     */
    bool IsRunning() const { return isRunning_; }

    /**
     * @brief Get epoll file descriptor
     */
    int32_t GetEpollFd() const { return epollFd_; }

private:
    void AcceptConnection();
    void HandleClientData(int32_t fd);
    void RemoveClient(int32_t fd);
    bool SetNonBlocking(int32_t fd);

    int32_t serverFd_;
    int32_t epollFd_;
    int32_t timerFd_;
    bool isRunning_;
    TcpCallbacks callbacks_;
    std::function<void()> timerCallback_;
    std::unordered_map<int32_t, std::string> clientIps_;
};

}  // namespace server

#endif  // TCP_SERVER_H
