#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <cstdint>
#include <string>
#include <vector>

namespace server {

/**
 * @brief WebSocket opcodes (RFC 6455)
 */
enum class WsOpcode : uint8_t {
    CONTINUATION = 0x0,
    TEXT = 0x1,
    BINARY = 0x2,
    CLOSE = 0x8,
    PING = 0x9,
    PONG = 0xA
};

/**
 * @brief WebSocket frame structure
 */
struct WsFrame {
    bool fin;
    WsOpcode opcode;
    bool masked;
    uint64_t payloadLen;
    uint8_t maskKey[4];
    std::vector<uint8_t> payload;
};

/**
 * @brief WebSocket protocol handler
 */
class WebSocket {
public:
    /**
     * @brief Check if data is HTTP upgrade request
     * @param data raw data
     * @param len data length
     * @return true if HTTP request
     */
    static bool IsHttpRequest(const uint8_t* data, size_t len);

    /**
     * @brief Parse HTTP upgrade request and generate response
     * @param request HTTP request string
     * @param response output response string
     * @return true on success
     */
    static bool HandleHandshake(const std::string& request, std::string& response);

    /**
     * @brief Parse WebSocket frame from raw data
     * @param data raw data
     * @param len data length
     * @param frame output frame structure
     * @param consumed output bytes consumed
     * @return true if complete frame parsed
     */
    static bool ParseFrame(const uint8_t* data, size_t len, WsFrame& frame, size_t& consumed);

    /**
     * @brief Encode data as WebSocket frame
     * @param opcode frame opcode
     * @param payload payload data
     * @param len payload length
     * @return encoded frame data
     */
    static std::vector<uint8_t> EncodeFrame(WsOpcode opcode, const uint8_t* payload, size_t len);

    /**
     * @brief Create close frame
     * @param code close status code
     * @param reason close reason
     * @return encoded close frame
     */
    static std::vector<uint8_t> CreateCloseFrame(uint16_t code, const std::string& reason = "");

    /**
     * @brief Create pong frame
     * @param pingPayload payload from ping frame
     * @return encoded pong frame
     */
    static std::vector<uint8_t> CreatePongFrame(const std::vector<uint8_t>& pingPayload);

private:
    static std::string ComputeAcceptKey(const std::string& key);
    static std::string Base64Encode(const uint8_t* data, size_t len);
    static void Sha1(const uint8_t* data, size_t len, uint8_t* output);
};

}  // namespace server

#endif  // WEBSOCKET_H
