#ifndef CONNECTION_H
#define CONNECTION_H

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace server {

/**
 * @brief Connection state
 */
enum class ConnState {
    HANDSHAKING_TLS,
    HANDSHAKING_WS,
    CONNECTED,
    NEGOTIATING,
    STREAMING,
    CLOSING
};

/**
 * @brief Connection statistics
 */
struct ConnStats {
    uint64_t messagesSent;
    uint64_t bytesSent;
    std::chrono::steady_clock::time_point connectedAt;
};

/**
 * @brief Client connection info
 */
struct Connection {
    int32_t fd;
    int32_t id;
    std::string ip;
    ConnState state;
    ConnStats stats;
    size_t auIndex;
    std::vector<uint8_t> recvBuffer;
    std::chrono::steady_clock::time_point negotiateOfferTime;
};

/**
 * @brief Connection manager
 */
class ConnectionManager {
public:
    ConnectionManager();

    /**
     * @brief Add new connection
     * @param fd file descriptor
     * @param ip client IP address
     * @return connection ID
     */
    int32_t AddConnection(int32_t fd, const std::string& ip);

    /**
     * @brief Remove connection
     * @param fd file descriptor
     */
    void RemoveConnection(int32_t fd);

    /**
     * @brief Get connection by fd
     * @param fd file descriptor
     * @return pointer to connection, nullptr if not found
     */
    Connection* GetConnection(int32_t fd);

    /**
     * @brief Get all connections
     */
    std::unordered_map<int32_t, Connection>& GetConnections() { return connections_; }

    /**
     * @brief Get connection count
     */
    size_t GetConnectionCount() const { return connections_.size(); }

    /**
     * @brief Get total connection count since start
     */
    int32_t GetTotalConnections() const { return totalConnections_; }

    /**
     * @brief Log connection statistics
     * @param fd file descriptor
     */
    void LogConnectionStats(int32_t fd) const;

    /**
     * @brief Log server status
     */
    void LogServerStatus() const;

private:
    std::unordered_map<int32_t, Connection> connections_;
    int32_t totalConnections_;
};

}  // namespace server

#endif  // CONNECTION_H
