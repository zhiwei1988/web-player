#ifndef FRAME_PROTOCOL_H
#define FRAME_PROTOCOL_H

#include <cstdint>
#include <vector>

namespace server {

// Protocol constants
static const uint16_t PROTOCOL_MAGIC        = 0xEB01;
static const uint8_t  PROTOCOL_VERSION      = 1;
static const uint8_t  FIXED_HEADER_SIZE     = 20;
static const uint16_t FRAGMENT_THRESHOLD    = 16384;  // 16KB

// msg_type
enum class MsgType : uint8_t {
    VIDEO    = 0x01,
    AUDIO    = 0x02,
    IMAGE    = 0x03,
    METADATA = 0x04,
    CONTROL  = 0x05
};

// flags bit definitions
enum FrameFlag : uint8_t {
    FLAG_FRAGMENT   = 0x01,  // bit 0
    FLAG_ENCRYPTED  = 0x02,  // bit 1
    FLAG_COMPRESSED = 0x04,  // bit 2
    FLAG_HAS_COMMON = 0x08   // bit 3
};

// common_flags bit definitions
enum CommonFlag : uint8_t {
    COMMON_ABS_TIME   = 0x01,  // bit 0: abs_time (8 bytes)
    COMMON_WATERMARK  = 0x02,  // bit 1: watermark (4 bytes)
    COMMON_SEQ_NUMBER = 0x04   // bit 2: seq_number (4 bytes)
};

// Video codec types (in video ext header)
enum class VideoCodec : uint8_t {
    H264  = 1,
    H265  = 2,
    MJPEG = 3
};

// Video frame types (in video ext header)
enum class VideoFrameType : uint8_t {
    IDR     = 1,
    I_FRAME = 2,
    P_FRAME = 3,
    B_FRAME = 4,
    SPS_PPS = 5,
    VPS     = 6
};

class FrameProtocol {
public:
    /**
     * Encode an Access Unit into one or more protocol frames.
     * If payload > FRAGMENT_THRESHOLD, generates multiple fragments.
     *
     * @param payload      merged NAL unit data
     * @param codec        video codec type
     * @param frameType    video frame type
     * @param timestampMs  relative timestamp in ms
     * @param absTimeMs    absolute UTC timestamp in ms
     * @param frameId      frame ID for fragmentation tracking
     * @return vector of encoded protocol frames (each is a complete binary frame)
     */
    static std::vector<std::vector<uint8_t>> EncodeVideoFrame(
        const std::vector<uint8_t>& payload,
        VideoCodec codec,
        VideoFrameType frameType,
        int64_t timestampMs,
        int64_t absTimeMs,
        uint16_t frameId);

private:
    static void WriteFixedHeader(std::vector<uint8_t>& buf,
                                 MsgType msgType,
                                 uint8_t flags,
                                 int64_t timestamp,
                                 uint8_t extLength,
                                 uint32_t payloadLength);

    static void WriteCommonExtHeader(std::vector<uint8_t>& buf,
                                     int64_t absTimeMs);

    static void WriteVideoExtHeader(std::vector<uint8_t>& buf,
                                    VideoCodec codec,
                                    VideoFrameType frameType);

    static void WriteFragmentExtHeader(std::vector<uint8_t>& buf,
                                       uint16_t frameId,
                                       uint16_t fragmentIndex,
                                       uint16_t totalFragments);

    static void WriteBE16(std::vector<uint8_t>& buf, uint16_t val);
    static void WriteBE32(std::vector<uint8_t>& buf, uint32_t val);
    static void WriteBE64(std::vector<uint8_t>& buf, int64_t val);
};

}  // namespace server

#endif  // FRAME_PROTOCOL_H
