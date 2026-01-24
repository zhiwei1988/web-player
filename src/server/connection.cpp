#include "connection.h"

#include <cstdio>

namespace server {

ConnectionManager::ConnectionManager() : totalConnections_(0) {
}

int32_t ConnectionManager::AddConnection(int32_t fd, const std::string& ip) {
    int32_t id = ++totalConnections_;

    Connection conn;
    conn.fd = fd;
    conn.id = id;
    conn.ip = ip;
    conn.state = ConnState::HANDSHAKING;
    conn.nalIndex = 0;
    conn.stats.messagesSent = 0;
    conn.stats.bytesSent = 0;
    conn.stats.connectedAt = std::chrono::steady_clock::now();

    connections_[fd] = std::move(conn);

    std::printf("[Connection #%d] New client connected\n", id);
    std::printf("   IP Address: %s\n", ip.c_str());
    std::printf("   Current connections: %zu\n\n", connections_.size());

    return id;
}

void ConnectionManager::RemoveConnection(int32_t fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) {
        return;
    }

    LogConnectionStats(fd);
    connections_.erase(it);
}

Connection* ConnectionManager::GetConnection(int32_t fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) {
        return nullptr;
    }
    return &it->second;
}

void ConnectionManager::LogConnectionStats(int32_t fd) const {
    auto it = connections_.find(fd);
    if (it == connections_.end()) {
        return;
    }

    const Connection& conn = it->second;
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
        now - conn.stats.connectedAt).count();
    double mbSent = conn.stats.bytesSent / 1024.0 / 1024.0;

    std::printf("\n[Connection #%d] Client disconnected\n", conn.id);
    std::printf("   Connection duration: %lld seconds\n", static_cast<long long>(duration));
    std::printf("   Messages sent: %llu\n", static_cast<unsigned long long>(conn.stats.messagesSent));
    std::printf("   Data sent: %.2f MB\n", mbSent);
    std::printf("   Remaining connections: %zu\n\n", connections_.size() - 1);
}

void ConnectionManager::LogServerStatus() const {
    if (connections_.empty()) {
        return;
    }

    uint64_t totalBytesSent = 0;
    uint64_t totalMessagesSent = 0;

    for (const auto& pair : connections_) {
        totalBytesSent += pair.second.stats.bytesSent;
        totalMessagesSent += pair.second.stats.messagesSent;
    }

    std::printf("\nServer status:\n");
    std::printf("   Active connections: %zu\n", connections_.size());
    std::printf("   Total connections: %d\n", totalConnections_);

    if (totalBytesSent > 0) {
        double mbSent = totalBytesSent / 1024.0 / 1024.0;
        std::printf("   Total sent: %llu messages, %.2f MB\n",
                    static_cast<unsigned long long>(totalMessagesSent), mbSent);
    }
    std::printf("\n");
}

}  // namespace server
