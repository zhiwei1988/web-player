#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <chrono>

#include "connection.h"
#include "frame_protocol.h"
#include "nal_parser.h"
#include "tls_server.h"
#include "timer.h"
#include "websocket.h"

using namespace server;

static const uint16_t DEFAULT_PORT = 6061;

static volatile bool gRunning = true;

static void SignalHandler(int sig) {
    (void)sig;
    gRunning = false;
}

class VideoServer {
public:
    VideoServer()
        : port_(DEFAULT_PORT),
          isH265_(false),
          frameId_(0),
          frameIntervalMs_(40.0),
          certPath_(""),
          keyPath_("") {
    }

    bool Initialize(int32_t argc, char* argv[]) {
        ParseArgs(argc, argv);

        std::printf("Codec type: %s\n", isH265_ ? "H.265/HEVC" : "H.264/AVC");
        std::printf("Video file: %s\n", videoPath_.c_str());

        if (!nalParser_.LoadFile(videoPath_, isH265_)) {
            return false;
        }

        if (!tlsServer_.Start(port_, certPath_, keyPath_)) {
            return false;
        }

        // Calculate timer interval from detected frame rate
        double fps = nalParser_.GetFrameRate();
        frameIntervalMs_ = 1000.0 / fps;
        uint32_t intervalMs = static_cast<uint32_t>(frameIntervalMs_);

        std::printf("Send interval: %u ms (%.2f fps)\n", intervalMs, fps);

        if (!timer_.Start(intervalMs)) {
            return false;
        }

        tlsServer_.RegisterTimer(timer_.GetFd());
        SetupCallbacks();

        return true;
    }

    void Run() {
        std::printf("\nWebSocket server running on port %u\n", port_);
        std::printf("Press Ctrl+C to stop\n\n");

        while (gRunning && tlsServer_.IsRunning()) {
            tlsServer_.ProcessEvents(1000);
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
            } else if (std::strcmp(argv[i], "--cert") == 0 && i + 1 < argc) {
                certPath_ = argv[i + 1];
                ++i;
            } else if (std::strcmp(argv[i], "--key") == 0 && i + 1 < argc) {
                keyPath_ = argv[i + 1];
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

        // Validate cert/key pairing
        if (!certPath_.empty() && keyPath_.empty()) {
            std::fprintf(stderr, "Error: --cert specified without --key\n");
            std::exit(1);
        }
        if (certPath_.empty() && !keyPath_.empty()) {
            std::fprintf(stderr, "Error: --key specified without --cert\n");
            std::exit(1);
        }
    }

    void PrintUsage(const char* program) {
        std::printf("Usage: %s [options]\n", program);
        std::printf("Options:\n");
        std::printf("  -p <port>      Port number (default: %u)\n", DEFAULT_PORT);
        std::printf("  -c <codec>     Codec type: h264, h265 (default: h264)\n");
        std::printf("  -f <file>      Video file path\n");
        std::printf("  --cert <file>  TLS certificate file (PEM format)\n");
        std::printf("  --key <file>   TLS private key file (PEM format)\n");
        std::printf("  -h             Show this help\n");
        std::printf("\nTLS:\n");
        std::printf("  Both --cert and --key must be specified together.\n");
        std::printf("  If not specified, a self-signed certificate will be generated.\n");
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

        tlsServer_.SetCallbacks(callbacks);

        tlsServer_.SetTimerCallback([this]() {
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

        if (conn->state == ConnState::HANDSHAKING_WS) {
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
            tlsServer_.CloseConnection(fd);
            return;
        }

        std::string response;
        if (!WebSocket::HandleHandshake(request, response)) {
            tlsServer_.CloseConnection(fd);
            return;
        }

        tlsServer_.SendData(fd, reinterpret_cast<const uint8_t*>(response.data()),
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
                    tlsServer_.SendData(fd, pong.data(), pong.size());
                    break;
                }
                case WsOpcode::CLOSE:
                    conn->state = ConnState::CLOSING;
                    tlsServer_.CloseConnection(fd);
                    return;
                default:
                    break;
            }
        }
    }

    VideoFrameType DetectFrameType(const AccessUnit& au) {
        for (const auto& nal : au.nalUnits) {
            size_t offset = 0;
            if (nal.data.size() >= 4 && nal.data[0] == 0 && nal.data[1] == 0) {
                if (nal.data[2] == 0 && nal.data[3] == 1) {
                    offset = 4;
                } else if (nal.data[2] == 1) {
                    offset = 3;
                }
            }
            if (offset == 0 || offset >= nal.data.size()) {
                continue;
            }

            if (isH265_) {
                uint8_t nalType = (nal.data[offset] >> 1) & 0x3F;
                if (nalType == 32) return VideoFrameType::VPS;
                if (nalType == 33 || nalType == 34) return VideoFrameType::SPS_PPS;
                // IDR_W_RADL(19), IDR_N_LP(20)
                if (nalType == 19 || nalType == 20) return VideoFrameType::IDR;
                // BLA/CRA (16-23 are IRAP)
                if (nalType >= 16 && nalType <= 23) return VideoFrameType::I_FRAME;
                // TRAIL_R(1), TSA_R(3), STSA_R(5) etc - treated as P
                if (nalType >= 0 && nalType <= 15) return VideoFrameType::P_FRAME;
            } else {
                uint8_t nalType = nal.data[offset] & 0x1F;
                if (nalType == 7 || nalType == 8) return VideoFrameType::SPS_PPS;
                if (nalType == 5) return VideoFrameType::IDR;
                if (nalType == 1) return VideoFrameType::P_FRAME;
            }
        }
        return VideoFrameType::P_FRAME;
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

            // Merge all NAL units into a single payload
            std::vector<uint8_t> payload;
            for (const auto& nal : au->nalUnits) {
                payload.insert(payload.end(), nal.data.begin(), nal.data.end());
            }

            VideoCodec codec = isH265_ ? VideoCodec::H265 : VideoCodec::H264;
            VideoFrameType frameType = DetectFrameType(*au);

            int64_t timestampMs = static_cast<int64_t>(conn.auIndex * frameIntervalMs_);
            auto now = std::chrono::system_clock::now();
            int64_t absTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();

            auto protocolFrames = FrameProtocol::EncodeVideoFrame(
                payload, codec, frameType, timestampMs, absTimeMs, frameId_);

            for (const auto& protoFrame : protocolFrames) {
                auto wsFrame = WebSocket::EncodeFrame(WsOpcode::BINARY,
                                                      protoFrame.data(), protoFrame.size());

                int32_t sent = tlsServer_.SendData(conn.fd, wsFrame.data(), wsFrame.size());
                if (sent > 0) {
                    conn.stats.messagesSent++;
                    conn.stats.bytesSent += protoFrame.size();
                }
            }

            conn.auIndex++;
        }

        frameId_++;
    }

    void Shutdown() {
        std::printf("\nShutting down server...\n");

        // Send close frame to all clients
        auto closeFrame = WebSocket::CreateCloseFrame(1000, "Server is shutting down");
        for (auto& pair : connManager_.GetConnections()) {
            tlsServer_.SendData(pair.first, closeFrame.data(), closeFrame.size());
        }

        timer_.Stop();
        tlsServer_.Stop();

        std::printf("Server closed\n");
    }

    TlsServer tlsServer_;
    Timer timer_;
    NalParser nalParser_;
    ConnectionManager connManager_;
    uint16_t port_;
    bool isH265_;
    uint16_t frameId_;
    double frameIntervalMs_;
    std::string videoPath_;
    std::string certPath_;
    std::string keyPath_;
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
