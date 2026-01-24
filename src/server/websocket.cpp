#include "websocket.h"

#include <mbedtls/base64.h>
#include <mbedtls/sha1.h>

#include <cstring>

namespace server {

static const char* WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

bool WebSocket::IsHttpRequest(const uint8_t* data, size_t len) {
    if (len < 4) {
        return false;
    }
    return std::memcmp(data, "GET ", 4) == 0;
}

void WebSocket::Sha1(const uint8_t* data, size_t len, uint8_t* output) {
    mbedtls_sha1_context ctx;
    mbedtls_sha1_init(&ctx);
    mbedtls_sha1_starts_ret(&ctx);
    mbedtls_sha1_update_ret(&ctx, data, len);
    mbedtls_sha1_finish_ret(&ctx, output);
    mbedtls_sha1_free(&ctx);
}

std::string WebSocket::Base64Encode(const uint8_t* data, size_t len) {
    size_t outLen = 0;
    mbedtls_base64_encode(nullptr, 0, &outLen, data, len);

    std::vector<uint8_t> output(outLen);
    mbedtls_base64_encode(output.data(), outLen, &outLen, data, len);

    return std::string(reinterpret_cast<char*>(output.data()), outLen);
}

std::string WebSocket::ComputeAcceptKey(const std::string& key) {
    std::string combined = key + WS_GUID;

    uint8_t sha1Hash[20];
    Sha1(reinterpret_cast<const uint8_t*>(combined.c_str()), combined.length(), sha1Hash);

    return Base64Encode(sha1Hash, 20);
}

bool WebSocket::HandleHandshake(const std::string& request, std::string& response) {
    // Find Sec-WebSocket-Key header
    const char* keyHeader = "Sec-WebSocket-Key:";
    size_t keyPos = request.find(keyHeader);
    if (keyPos == std::string::npos) {
        return false;
    }

    keyPos += std::strlen(keyHeader);
    while (keyPos < request.length() && request[keyPos] == ' ') {
        ++keyPos;
    }

    size_t keyEnd = request.find("\r\n", keyPos);
    if (keyEnd == std::string::npos) {
        return false;
    }

    std::string wsKey = request.substr(keyPos, keyEnd - keyPos);

    // Trim trailing spaces
    while (!wsKey.empty() && wsKey.back() == ' ') {
        wsKey.pop_back();
    }

    std::string acceptKey = ComputeAcceptKey(wsKey);

    response = "HTTP/1.1 101 Switching Protocols\r\n"
               "Upgrade: websocket\r\n"
               "Connection: Upgrade\r\n"
               "Sec-WebSocket-Accept: " + acceptKey + "\r\n"
               "\r\n";

    return true;
}

bool WebSocket::ParseFrame(const uint8_t* data, size_t len, WsFrame& frame, size_t& consumed) {
    if (len < 2) {
        return false;
    }

    size_t offset = 0;

    frame.fin = (data[0] & 0x80) != 0;
    frame.opcode = static_cast<WsOpcode>(data[0] & 0x0F);
    frame.masked = (data[1] & 0x80) != 0;

    uint64_t payloadLen = data[1] & 0x7F;
    offset = 2;

    if (payloadLen == 126) {
        if (len < 4) {
            return false;
        }
        payloadLen = (static_cast<uint64_t>(data[2]) << 8) | data[3];
        offset = 4;
    } else if (payloadLen == 127) {
        if (len < 10) {
            return false;
        }
        payloadLen = 0;
        for (int32_t i = 0; i < 8; ++i) {
            payloadLen = (payloadLen << 8) | data[2 + i];
        }
        offset = 10;
    }

    frame.payloadLen = payloadLen;

    if (frame.masked) {
        if (len < offset + 4) {
            return false;
        }
        std::memcpy(frame.maskKey, data + offset, 4);
        offset += 4;
    }

    if (len < offset + payloadLen) {
        return false;
    }

    frame.payload.resize(static_cast<size_t>(payloadLen));
    std::memcpy(frame.payload.data(), data + offset, static_cast<size_t>(payloadLen));

    // Unmask payload
    if (frame.masked) {
        for (size_t i = 0; i < frame.payload.size(); ++i) {
            frame.payload[i] ^= frame.maskKey[i % 4];
        }
    }

    consumed = offset + static_cast<size_t>(payloadLen);
    return true;
}

std::vector<uint8_t> WebSocket::EncodeFrame(WsOpcode opcode, const uint8_t* payload, size_t len) {
    std::vector<uint8_t> frame;

    // FIN + opcode
    frame.push_back(0x80 | static_cast<uint8_t>(opcode));

    // Payload length (server doesn't mask)
    if (len < 126) {
        frame.push_back(static_cast<uint8_t>(len));
    } else if (len <= 0xFFFF) {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        frame.push_back(127);
        for (int32_t i = 7; i >= 0; --i) {
            frame.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
        }
    }

    // Payload
    frame.insert(frame.end(), payload, payload + len);

    return frame;
}

std::vector<uint8_t> WebSocket::CreateCloseFrame(uint16_t code, const std::string& reason) {
    std::vector<uint8_t> payload;
    payload.push_back(static_cast<uint8_t>((code >> 8) & 0xFF));
    payload.push_back(static_cast<uint8_t>(code & 0xFF));
    payload.insert(payload.end(), reason.begin(), reason.end());

    return EncodeFrame(WsOpcode::CLOSE, payload.data(), payload.size());
}

std::vector<uint8_t> WebSocket::CreatePongFrame(const std::vector<uint8_t>& pingPayload) {
    return EncodeFrame(WsOpcode::PONG, pingPayload.data(), pingPayload.size());
}

}  // namespace server
