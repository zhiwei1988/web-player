#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "connection.h"
#include "nal_parser.h"
#include "tcp_server.h"
#include "timer.h"
#include "websocket.h"

using namespace server;

static const uint16_t DEFAULT_PORT = 8080;

static volatile bool gRunning = true;

static void SignalHandler(int sig) {
    (void)sig;
    gRunning = false;
}

class VideoServer {
public:
    VideoServer()
        : port_(DEFAULT_PORT),
          isH265_(false) {
    }

    bool Initialize(int32_t argc, char* argv[]) {
        ParseArgs(argc, argv);

        std::printf("Codec type: %s\n", isH265_ ? "H.265/HEVC" : "H.264/AVC");
        std::printf("Video file: %s\n", videoPath_.c_str());

        if (!nalParser_.LoadFile(videoPath_, isH265_)) {
            return false;
        }

        if (!tcpServer_.Start(port_)) {
            return false;
        }

        // Calculate timer interval from detected frame rate
        double fps = nalParser_.GetFrameRate();
        uint32_t intervalMs = static_cast<uint32_t>(1000.0 / fps);

        std::printf("Send interval: %u ms (%.2f fps)\n", intervalMs, fps);

        if (!timer_.Start(intervalMs)) {
            return false;
        }

        tcpServer_.RegisterTimer(timer_.GetFd());
        SetupCallbacks();

        return true;
    }

    void Run() {
        std::printf("\nWebSocket server running on port %u\n", port_);
        std::printf("Press Ctrl+C to stop\n\n");

        while (gRunning && tcpServer_.IsRunning()) {
            tcpServer_.ProcessEvents(1000);
        }

        Shutdown();
    }

private:
    void ParseArgs(int32_t argc, char* argv[]) {
        // Check CODEC_TYPE env var
        const char* codecEnv = std::getenv("CODEC_TYPE");
        if (codecEnv != nullptr) {
            isH265_ = (std::strcmp(codecEnv, "h265") == 0 ||
                       std::strcmp(codecEnv, "hevc") == 0);
        }

        // Parse command line args
        for (int32_t i = 1; i < argc; ++i) {
            if (std::strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
                port_ = static_cast<uint16_t>(std::atoi(argv[i + 1]));
                ++i;
            } else if (std::strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
                const char* codec = argv[i + 1];
                isH265_ = (std::strcmp(codec, "h265") == 0 ||
                           std::strcmp(codec, "hevc") == 0);
                ++i;
            } else if (std::strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
                videoPath_ = argv[i + 1];
                ++i;
            } else if (std::strcmp(argv[i], "-h") == 0) {
                PrintUsage(argv[0]);
                std::exit(0);
            }
        }

        // Set default video path if not specified
        if (videoPath_.empty()) {
            videoPath_ = isH265_ ? "./tests/fixtures/TSU_640x360.h265"
                                 : "./tests/fixtures/test_video.h264";
        }
    }

    void PrintUsage(const char* program) {
        std::printf("Usage: %s [options]\n", program);
        std::printf("Options:\n");
        std::printf("  -p <port>   Port number (default: %u)\n", DEFAULT_PORT);
        std::printf("  -c <codec>  Codec type: h264, h265 (default: h264)\n");
        std::printf("  -f <file>   Video file path\n");
        std::printf("  -h          Show this help\n");
        std::printf("\nEnvironment:\n");
        std::printf("  CODEC_TYPE  Codec type (h264 or h265)\n");
    }

    void SetupCallbacks() {
        TcpCallbacks callbacks;

        callbacks.onConnect = [this](int32_t fd, const std::string& ip) {
            connManager_.AddConnection(fd, ip);
        };

        callbacks.onDisconnect = [this](int32_t fd) {
            connManager_.RemoveConnection(fd);
        };

        callbacks.onData = [this](int32_t fd, const uint8_t* data, size_t len) {
            HandleData(fd, data, len);
        };

        tcpServer_.SetCallbacks(callbacks);

        tcpServer_.SetTimerCallback([this]() {
            OnTimer();
        });
    }

    void HandleData(int32_t fd, const uint8_t* data, size_t len) {
        Connection* conn = connManager_.GetConnection(fd);
        if (conn == nullptr) {
            return;
        }

        // Append to receive buffer
        conn->recvBuffer.insert(conn->recvBuffer.end(), data, data + len);

        if (conn->state == ConnState::HANDSHAKING) {
            HandleHandshake(fd, conn);
        } else if (conn->state == ConnState::CONNECTED) {
            HandleWebSocketFrame(fd, conn);
        }
    }

    void HandleHandshake(int32_t fd, Connection* conn) {
        // Check for complete HTTP request
        std::string request(conn->recvBuffer.begin(), conn->recvBuffer.end());
        if (request.find("\r\n\r\n") == std::string::npos) {
            return;
        }

        if (!WebSocket::IsHttpRequest(conn->recvBuffer.data(), conn->recvBuffer.size())) {
            tcpServer_.CloseConnection(fd);
            return;
        }

        std::string response;
        if (!WebSocket::HandleHandshake(request, response)) {
            tcpServer_.CloseConnection(fd);
            return;
        }

        tcpServer_.SendData(fd, reinterpret_cast<const uint8_t*>(response.data()),
                            response.size());

        conn->recvBuffer.clear();
        conn->state = ConnState::CONNECTED;

        std::printf("[Connection #%d] WebSocket handshake completed\n", conn->id);
    }

    void HandleWebSocketFrame(int32_t fd, Connection* conn) {
        while (!conn->recvBuffer.empty()) {
            WsFrame frame;
            size_t consumed = 0;

            if (!WebSocket::ParseFrame(conn->recvBuffer.data(), conn->recvBuffer.size(),
                                       frame, consumed)) {
                break;
            }

            // Remove consumed data
            conn->recvBuffer.erase(conn->recvBuffer.begin(),
                                   conn->recvBuffer.begin() + consumed);

            // Handle frame
            switch (frame.opcode) {
                case WsOpcode::TEXT: {
                    std::string msg(frame.payload.begin(), frame.payload.end());
                    std::printf("[Connection #%d] Received message: %s\n", conn->id, msg.c_str());
                    break;
                }
                case WsOpcode::BINARY:
                    std::printf("[Connection #%d] Received binary: %zu bytes\n",
                                conn->id, frame.payload.size());
                    break;
                case WsOpcode::PING: {
                    auto pong = WebSocket::CreatePongFrame(frame.payload);
                    tcpServer_.SendData(fd, pong.data(), pong.size());
                    break;
                }
                case WsOpcode::CLOSE:
                    conn->state = ConnState::CLOSING;
                    tcpServer_.CloseConnection(fd);
                    return;
                default:
                    break;
            }
        }
    }

    void OnTimer() {
        timer_.Read();

        for (auto& pair : connManager_.GetConnections()) {
            Connection& conn = pair.second;

            if (conn.state != ConnState::CONNECTED) {
                continue;
            }

            if (nalParser_.GetAccessUnitCount() == 0) {
                continue;
            }

            size_t auIndex = conn.auIndex % nalParser_.GetAccessUnitCount();
            const AccessUnit* au = nalParser_.GetAccessUnit(auIndex);

            if (au == nullptr) {
                continue;
            }

            // Log every 25 Access Units
            if (auIndex % 25 == 0) {
                std::printf("[Connection #%d] Sending AU %zu/%zu (%zu NAL units)\n",
                            conn.id, auIndex, nalParser_.GetAccessUnitCount(), au->nalUnits.size());
            }

            // Send all NAL units in this Access Unit
            for (const auto& nal : au->nalUnits) {
                auto frame = WebSocket::EncodeFrame(WsOpcode::BINARY,
                                                    nal.data.data(), nal.data.size());

                int32_t sent = tcpServer_.SendData(conn.fd, frame.data(), frame.size());
                if (sent > 0) {
                    conn.stats.messagesSent++;
                    conn.stats.bytesSent += nal.data.size();
                }
            }

            conn.auIndex++;
        }
    }

    void Shutdown() {
        std::printf("\nShutting down server...\n");

        // Send close frame to all clients
        auto closeFrame = WebSocket::CreateCloseFrame(1000, "Server is shutting down");
        for (auto& pair : connManager_.GetConnections()) {
            tcpServer_.SendData(pair.first, closeFrame.data(), closeFrame.size());
        }

        timer_.Stop();
        tcpServer_.Stop();

        std::printf("Server closed\n");
    }

    TcpServer tcpServer_;
    Timer timer_;
    NalParser nalParser_;
    ConnectionManager connManager_;
    uint16_t port_;
    bool isH265_;
    std::string videoPath_;
};

int main(int argc, char* argv[]) {
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    VideoServer server;
    if (!server.Initialize(argc, argv)) {
        return 1;
    }

    server.Run();
    return 0;
}
