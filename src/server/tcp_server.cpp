#include "tcp_server.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace server {

static const int32_t MAX_EVENTS = 64;
static const int32_t RECV_BUFFER_SIZE = 65536;

TcpServer::TcpServer()
    : serverFd_(-1),
      epollFd_(-1),
      timerFd_(-1),
      isRunning_(false) {
}

TcpServer::~TcpServer() {
    Stop();
}

bool TcpServer::SetNonBlocking(int32_t fd) {
    int32_t flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return false;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

bool TcpServer::Start(uint16_t port) {
    serverFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd_ < 0) {
        std::fprintf(stderr, "Failed to create socket: %s\n", std::strerror(errno));
        return false;
    }

    int32_t opt = 1;
    if (setsockopt(serverFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::fprintf(stderr, "Failed to set SO_REUSEADDR: %s\n", std::strerror(errno));
        close(serverFd_);
        return false;
    }

    if (!SetNonBlocking(serverFd_)) {
        std::fprintf(stderr, "Failed to set non-blocking: %s\n", std::strerror(errno));
        close(serverFd_);
        return false;
    }

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(serverFd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::fprintf(stderr, "Failed to bind port %u: %s\n", port, std::strerror(errno));
        close(serverFd_);
        return false;
    }

    if (listen(serverFd_, SOMAXCONN) < 0) {
        std::fprintf(stderr, "Failed to listen: %s\n", std::strerror(errno));
        close(serverFd_);
        return false;
    }

    epollFd_ = epoll_create1(0);
    if (epollFd_ < 0) {
        std::fprintf(stderr, "Failed to create epoll: %s\n", std::strerror(errno));
        close(serverFd_);
        return false;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = serverFd_;
    if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, serverFd_, &ev) < 0) {
        std::fprintf(stderr, "Failed to add server to epoll: %s\n", std::strerror(errno));
        close(epollFd_);
        close(serverFd_);
        return false;
    }

    isRunning_ = true;
    std::printf("TCP server listening on port %u\n", port);
    return true;
}

void TcpServer::Stop() {
    isRunning_ = false;

    if (epollFd_ >= 0) {
        close(epollFd_);
        epollFd_ = -1;
    }

    if (serverFd_ >= 0) {
        close(serverFd_);
        serverFd_ = -1;
    }
}

void TcpServer::RegisterTimer(int32_t timerFd) {
    timerFd_ = timerFd;
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = timerFd;
    if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, timerFd, &ev) < 0) {
        std::fprintf(stderr, "Failed to add timer to epoll: %s\n", std::strerror(errno));
    }
}

void TcpServer::SetCallbacks(const TcpCallbacks& callbacks) {
    callbacks_ = callbacks;
}

void TcpServer::SetTimerCallback(std::function<void()> callback) {
    timerCallback_ = callback;
}

void TcpServer::ProcessEvents(int32_t timeoutMs) {
    struct epoll_event events[MAX_EVENTS];
    int32_t nfds = epoll_wait(epollFd_, events, MAX_EVENTS, timeoutMs);

    if (nfds < 0) {
        if (errno != EINTR) {
            std::fprintf(stderr, "epoll_wait error: %s\n", std::strerror(errno));
        }
        return;
    }

    for (int32_t i = 0; i < nfds; ++i) {
        int32_t fd = events[i].data.fd;

        if (fd == serverFd_) {
            AcceptConnection();
        } else if (fd == timerFd_) {
            if (timerCallback_) {
                timerCallback_();
            }
        } else {
            if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                RemoveClient(fd);
            } else if (events[i].events & EPOLLIN) {
                HandleClientData(fd);
            }
        }
    }
}

void TcpServer::AcceptConnection() {
    while (true) {
        struct sockaddr_in clientAddr;
        socklen_t addrLen = sizeof(clientAddr);
        int32_t clientFd = accept(serverFd_, reinterpret_cast<struct sockaddr*>(&clientAddr), &addrLen);

        if (clientFd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            std::fprintf(stderr, "Accept error: %s\n", std::strerror(errno));
            break;
        }

        if (!SetNonBlocking(clientFd)) {
            std::fprintf(stderr, "Failed to set client non-blocking\n");
            close(clientFd);
            continue;
        }

        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = clientFd;
        if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, clientFd, &ev) < 0) {
            std::fprintf(stderr, "Failed to add client to epoll: %s\n", std::strerror(errno));
            close(clientFd);
            continue;
        }

        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
        std::string clientIp(ipStr);
        clientIps_[clientFd] = clientIp;

        if (callbacks_.onConnect) {
            callbacks_.onConnect(clientFd, clientIp);
        }
    }
}

void TcpServer::HandleClientData(int32_t fd) {
    uint8_t buffer[RECV_BUFFER_SIZE];

    while (true) {
        ssize_t bytesRead = recv(fd, buffer, sizeof(buffer), 0);

        if (bytesRead < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            RemoveClient(fd);
            return;
        }

        if (bytesRead == 0) {
            RemoveClient(fd);
            return;
        }

        if (callbacks_.onData) {
            callbacks_.onData(fd, buffer, static_cast<size_t>(bytesRead));
        }
    }
}

void TcpServer::RemoveClient(int32_t fd) {
    epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);

    if (callbacks_.onDisconnect) {
        callbacks_.onDisconnect(fd);
    }

    clientIps_.erase(fd);
    close(fd);
}

int32_t TcpServer::SendData(int32_t fd, const uint8_t* data, size_t len) {
    size_t totalSent = 0;

    while (totalSent < len) {
        ssize_t sent = send(fd, data + totalSent, len - totalSent, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            return -1;
        }
        totalSent += static_cast<size_t>(sent);
    }

    return static_cast<int32_t>(totalSent);
}

void TcpServer::CloseConnection(int32_t fd) {
    RemoveClient(fd);
}

}  // namespace server
