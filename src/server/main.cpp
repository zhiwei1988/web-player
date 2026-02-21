#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <algorithm>
#include <chrono>
#include <ostream>
#include <string>
#include <iostream>

#include "connection.h"
#include "frame_protocol.h"
#include "mp4_demuxer.h"
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

static bool HasSuffix(const std::string& str, const std::string& suffix) {
    if (suffix.size() > str.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), str.rbegin());
}

static AudioCodec AudioCodecNameToEnum(const std::string& name) {
    if (name == "pcm_alaw") return AudioCodec::G711A;
    if (name == "pcm_mulaw") return AudioCodec::G711U;
    if (name == "g726") return AudioCodec::G726;
    if (name == "aac") return AudioCodec::AAC;
    return AudioCodec::AAC;
}

class VideoServer {
public:
    VideoServer()
        : port_(DEFAULT_PORT),
          isH265_(false),
          isMp4Mode_(false),
          frameId_(0),
          frameIntervalMs_(40.0),
          certPath_(""),
          keyPath_("") {
    }

    bool Initialize(int32_t argc, char* argv[]) {
        ParseArgs(argc, argv);

        isMp4Mode_ = HasSuffix(videoPath_, ".mp4");

        std::printf("Input file: %s (%s mode)\n",
                    videoPath_.c_str(), isMp4Mode_ ? "MP4" : "raw bitstream");

        if (isMp4Mode_) {
            if (!mp4Demuxer_.LoadFile(videoPath_)) {
                return false;
            }
            isH265_ = mp4Demuxer_.GetVideoInfo().isH265;
            double fps = mp4Demuxer_.GetFrameRate();
            frameIntervalMs_ = 1000.0 / fps;
        } else {
            std::printf("Codec type: %s\n", isH265_ ? "H.265/HEVC" : "H.264/AVC");
            if (!nalParser_.LoadFile(videoPath_, isH265_)) {
                return false;
            }
            double fps = nalParser_.GetFrameRate();
            frameIntervalMs_ = 1000.0 / fps;
        }

        if (!tlsServer_.Start(port_, certPath_, keyPath_)) {
            return false;
        }

        // For MP4 mode, use a fine-grained base timer (10ms)
        // For raw mode, use frame interval as before
        uint32_t timerIntervalMs = isMp4Mode_
            ? 10
            : static_cast<uint32_t>(frameIntervalMs_);

        std::printf("Timer interval: %u ms\n", timerIntervalMs);

        if (!timer_.Start(timerIntervalMs)) {
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
        std::printf("  -f <file>      Media file path (.mp4, .h264, .h265)\n");
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
        } else if (conn->state == ConnState::NEGOTIATING ||
                   conn->state == ConnState::STREAMING) {
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

        SendMediaOffer(fd, conn);
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
                    if (conn->state == ConnState::NEGOTIATING) {
                        HandleNegotiation(fd, conn, msg);
                    } else {
                        std::printf("[Connection #%d] Received text: %s\n", conn->id, msg.c_str());
                    }
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

    std::string BuildMediaOffer() const {
        const char* videoCodecStr = isH265_ ? "h265" : "h264";
        double fps = 1000.0 / frameIntervalMs_;

        char buf[512];

        if (isMp4Mode_ && mp4Demuxer_.GetAudioInfo().present) {
            const AudioInfo& audio = mp4Demuxer_.GetAudioInfo();
            std::snprintf(buf, sizeof(buf),
                "{\"type\":\"media-offer\",\"payload\":{\"version\":1,\"streams\":["
                "{\"type\":\"video\",\"codec\":\"%s\",\"framerate\":%.2f},"
                "{\"type\":\"audio\",\"codec\":\"%s\",\"sampleRate\":%d,\"channels\":%d}"
                "]}}",
                videoCodecStr, fps,
                audio.codecName.c_str(), audio.sampleRate, audio.channels);
        } else {
            std::snprintf(buf, sizeof(buf),
                "{\"type\":\"media-offer\",\"payload\":{\"version\":1,\"streams\":["
                "{\"type\":\"video\",\"codec\":\"%s\",\"framerate\":%.2f}"
                "]}}",
                videoCodecStr, fps);
        }

        return std::string(buf);
    }

    // Extracts the first string value for a given JSON key.
    static std::string ExtractJsonString(const std::string& json, const std::string& key) {
        std::string searchKey = "\"" + key + "\"";
        size_t pos = json.find(searchKey);
        if (pos == std::string::npos) return "";
        pos = json.find(':', pos + searchKey.size());
        if (pos == std::string::npos) return "";
        pos = json.find('"', pos + 1);
        if (pos == std::string::npos) return "";
        size_t end = json.find('"', pos + 1);
        if (end == std::string::npos) return "";
        return json.substr(pos + 1, end - pos - 1);
    }

    // Extracts a boolean value for a given JSON key.
    static bool ExtractJsonBool(const std::string& json, const std::string& key, bool defaultVal = false) {
        std::string searchKey = "\"" + key + "\"";
        size_t pos = json.find(searchKey);
        if (pos == std::string::npos) return defaultVal;
        pos = json.find(':', pos + searchKey.size());
        if (pos == std::string::npos) return defaultVal;
        pos++;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) pos++;
        if (pos + 4 <= json.size() && json.substr(pos, 4) == "true") return true;
        if (pos + 5 <= json.size() && json.substr(pos, 5) == "false") return false;
        return defaultVal;
    }

    void SendMediaOffer(int32_t fd, Connection* conn) {
        std::string offer = BuildMediaOffer();
        auto wsFrame = WebSocket::EncodeFrame(WsOpcode::TEXT,
            reinterpret_cast<const uint8_t*>(offer.data()), offer.size());
        tlsServer_.SendData(fd, wsFrame.data(), wsFrame.size());
        conn->state = ConnState::NEGOTIATING;
        conn->negotiateOfferTime = std::chrono::steady_clock::now();
        std::printf("[Connection #%d] Sent media-offer: %s\n", conn->id, offer.c_str());
    }

    void HandleNegotiation(int32_t fd, Connection* conn, const std::string& msg) {
        std::string type = ExtractJsonString(msg, "type");
        if (type != "media-answer") {
            std::printf("[Connection #%d] Unexpected message in NEGOTIATING state: type=%s\n",
                        conn->id, type.c_str());
            return;
        }
        bool accepted = ExtractJsonBool(msg, "accepted");
        if (accepted) {
            conn->state = ConnState::STREAMING;
            std::printf("[Connection #%d] Negotiation accepted, starting stream\n", conn->id);
        } else {
            std::string reason = ExtractJsonString(msg, "reason");
            std::printf("[Connection #%d] Negotiation rejected: %s\n", conn->id, reason.c_str());
            conn->state = ConnState::CLOSING;
            auto closeFrame = WebSocket::CreateCloseFrame(1000, "Negotiation rejected");
            tlsServer_.SendData(fd, closeFrame.data(), closeFrame.size());
            tlsServer_.CloseConnection(fd);
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

    // Detect video frame type from raw MP4 packet data (Annex B or AVCC)
    VideoFrameType DetectFrameTypeFromPacket(const std::vector<uint8_t>& data) {
        if (data.size() < 5) return VideoFrameType::P_FRAME;

        // Try Annex B start code
        size_t offset = 0;
        if (data[0] == 0 && data[1] == 0) {
            if (data[2] == 0 && data[3] == 1) {
                offset = 4;
            } else if (data[2] == 1) {
                offset = 3;
            }
        }

        // AVCC length-prefixed (4-byte length)
        if (offset == 0 && data.size() >= 5) {
            offset = 4;
        }

        if (offset >= data.size()) return VideoFrameType::P_FRAME;

        if (isH265_) {
            uint8_t nalType = (data[offset] >> 1) & 0x3F;
            if (nalType == 32) return VideoFrameType::VPS;
            if (nalType == 33 || nalType == 34) return VideoFrameType::SPS_PPS;
            if (nalType == 19 || nalType == 20) return VideoFrameType::IDR;
            if (nalType >= 16 && nalType <= 23) return VideoFrameType::I_FRAME;
        } else {
            uint8_t nalType = data[offset] & 0x1F;
            if (nalType == 7 || nalType == 8) return VideoFrameType::SPS_PPS;
            if (nalType == 5) return VideoFrameType::IDR;
        }
        return VideoFrameType::P_FRAME;
    }

    void SendPacket(Connection& conn, const MediaPacket& pkt) {
        auto now = std::chrono::system_clock::now();
        int64_t absTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();

        std::vector<std::vector<uint8_t>> protocolFrames;

        if (pkt.type == MediaType::VIDEO) {
            VideoCodec codec = isH265_ ? VideoCodec::H265 : VideoCodec::H264;
            VideoFrameType frameType = DetectFrameTypeFromPacket(pkt.data);

            protocolFrames = FrameProtocol::EncodeVideoFrame(
                pkt.data, codec, frameType, pkt.ptsMs, absTimeMs, frameId_);
        } else {
            const AudioInfo& audio = mp4Demuxer_.GetAudioInfo();
            AudioCodec audioCodec = AudioCodecNameToEnum(audio.codecName);
            SampleRateCode rateCode = FrameProtocol::SampleRateToCode(audio.sampleRate);
            uint8_t channels = static_cast<uint8_t>(audio.channels);

            protocolFrames = FrameProtocol::EncodeAudioFrame(
                pkt.data, audioCodec, rateCode, channels,
                pkt.ptsMs, absTimeMs, frameId_);
        }

        for (const auto& protoFrame : protocolFrames) {
            auto wsFrame = WebSocket::EncodeFrame(WsOpcode::BINARY,
                                                  protoFrame.data(), protoFrame.size());
            int32_t sent = tlsServer_.SendData(conn.fd, wsFrame.data(), wsFrame.size());
            if (sent > 0) {
                conn.stats.messagesSent++;
                conn.stats.bytesSent += protoFrame.size();
            }
        }

        frameId_++;
    }

    void OnTimerMp4(Connection& conn) {
        size_t packetCount = mp4Demuxer_.GetPacketCount();
        if (packetCount == 0) return;

        // Get the first packet's PTS as base for cyclic playback
        const MediaPacket* firstPkt = mp4Demuxer_.GetPacket(0);
        const MediaPacket* lastPkt = mp4Demuxer_.GetPacket(packetCount - 1);
        int64_t totalDurationMs = lastPkt->ptsMs - firstPkt->ptsMs;
        if (totalDurationMs <= 0) totalDurationMs = 1;

        // std::cout << "packetCount " << packetCount << " totalDurationMs " << totalDurationMs << std::endl;

        // Send all packets whose PTS <= current playback time
        while (true) {
            size_t idx = conn.packetIndex % packetCount;
            const MediaPacket* pkt = mp4Demuxer_.GetPacket(idx);
            if (pkt == nullptr) {
                break;
            }

            // Calculate effective PTS considering cyclic loops
            size_t loopCount = conn.packetIndex / packetCount;
            double effectivePtsMs = pkt->ptsMs - firstPkt->ptsMs
                                    + loopCount * totalDurationMs;

            if (effectivePtsMs > conn.playbackTimeMs) {
                // std::cerr << "packetIndex " << conn.packetIndex << " loopCount " << loopCount << std::endl;
                // std::cerr << "pkt pts " << pkt->ptsMs << " first pts " << firstPkt->ptsMs << std::endl;
                break;
            }

            SendPacket(conn, *pkt);
            conn.packetIndex++;
        }

        // Advance playback clock by timer interval (10ms)
        conn.playbackTimeMs += 10.0;
    }

    void OnTimerRaw(Connection& conn) {
        if (nalParser_.GetAccessUnitCount() == 0) return;

        size_t auIndex = conn.auIndex % nalParser_.GetAccessUnitCount();
        const AccessUnit* au = nalParser_.GetAccessUnit(auIndex);
        if (au == nullptr) return;

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
        frameId_++;
    }

    void OnTimer() {
        timer_.Read();

        // Collect timed-out connections to close after iteration
        std::vector<int32_t> negotiationTimeouts;

        for (auto& pair : connManager_.GetConnections()) {
            Connection& conn = pair.second;

            if (conn.state == ConnState::NEGOTIATING) {
                auto elapsed = std::chrono::steady_clock::now() - conn.negotiateOfferTime;
                if (elapsed > std::chrono::seconds(5)) {
                    std::printf("[Connection #%d] Negotiation timeout\n", conn.id);
                    auto closeFrame = WebSocket::CreateCloseFrame(1008, "Negotiation timeout");
                    tlsServer_.SendData(conn.fd, closeFrame.data(), closeFrame.size());
                    conn.state = ConnState::CLOSING;
                    negotiationTimeouts.push_back(conn.fd);
                }
                continue;
            }

            if (conn.state != ConnState::STREAMING) {
                continue;
            }

            if (isMp4Mode_) {
                OnTimerMp4(conn);
            } else {
                OnTimerRaw(conn);
            }
        }

        for (int32_t fd : negotiationTimeouts) {
            tlsServer_.CloseConnection(fd);
        }
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
    Mp4Demuxer mp4Demuxer_;
    ConnectionManager connManager_;
    uint16_t port_;
    bool isH265_;
    bool isMp4Mode_;
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
