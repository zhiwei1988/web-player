#include "frame_protocol.h"

#include <cstring>

namespace server {

void FrameProtocol::WriteBE16(std::vector<uint8_t>& buf, uint16_t val) {
    buf.push_back(static_cast<uint8_t>(val >> 8));
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
}

void FrameProtocol::WriteBE32(std::vector<uint8_t>& buf, uint32_t val) {
    buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
}

void FrameProtocol::WriteBE64(std::vector<uint8_t>& buf, int64_t val) {
    uint64_t uval = static_cast<uint64_t>(val);
    for (int i = 7; i >= 0; --i) {
        buf.push_back(static_cast<uint8_t>((uval >> (i * 8)) & 0xFF));
    }
}

void FrameProtocol::WriteFixedHeader(std::vector<uint8_t>& buf,
                                     MsgType msgType,
                                     uint8_t flags,
                                     int64_t timestamp,
                                     uint8_t extLength,
                                     uint32_t payloadLength) {
    // magic (2B)
    WriteBE16(buf, PROTOCOL_MAGIC);
    // version (1B)
    buf.push_back(PROTOCOL_VERSION);
    // msg_type (1B)
    buf.push_back(static_cast<uint8_t>(msgType));
    // flags (1B)
    buf.push_back(flags);
    // timestamp (8B)
    WriteBE64(buf, timestamp);
    // ext_length (1B)
    buf.push_back(extLength);
    // payload_length (4B)
    WriteBE32(buf, payloadLength);
    // reserved (2B)
    buf.push_back(0);
    buf.push_back(0);
}

void FrameProtocol::WriteCommonExtHeader(std::vector<uint8_t>& buf,
                                         int64_t absTimeMs) {
    // common_length = 1(self) + 1(common_flags) + 8(abs_time) = 10
    buf.push_back(10);
    // common_flags: bit0 = abs_time
    buf.push_back(COMMON_ABS_TIME);
    // abs_time (8B)
    WriteBE64(buf, absTimeMs);
}

void FrameProtocol::WriteVideoExtHeader(std::vector<uint8_t>& buf,
                                        VideoCodec codec,
                                        VideoFrameType frameType) {
    // codec (1B)
    buf.push_back(static_cast<uint8_t>(codec));
    // frame_type (1B)
    buf.push_back(static_cast<uint8_t>(frameType));
    // resolution (2B) - 0 means described in SPS
    WriteBE16(buf, 0);
}

void FrameProtocol::WriteFragmentExtHeader(std::vector<uint8_t>& buf,
                                           uint16_t frameId,
                                           uint16_t fragmentIndex,
                                           uint16_t totalFragments) {
    WriteBE16(buf, frameId);
    WriteBE16(buf, fragmentIndex);
    WriteBE16(buf, totalFragments);
}

std::vector<std::vector<uint8_t>> FrameProtocol::EncodeVideoFrame(
    const std::vector<uint8_t>& payload,
    VideoCodec codec,
    VideoFrameType frameType,
    int64_t timestampMs,
    int64_t absTimeMs,
    uint16_t frameId) {

    std::vector<std::vector<uint8_t>> frames;

    // Extension header sizes
    const uint8_t kCommonExtSize = 10;  // common_length(1) + common_flags(1) + abs_time(8)
    const uint8_t kVideoExtSize = 4;   // codec(1) + frame_type(1) + resolution(2)
    const uint8_t kFragExtSize = 6;    // frame_id(2) + fragment_index(2) + total_fragments(2)

    if (payload.size() <= FRAGMENT_THRESHOLD) {
        // Single frame: fixed header + common ext + video ext + payload
        uint8_t extLength = kCommonExtSize + kVideoExtSize;
        uint8_t flags = FLAG_HAS_COMMON;

        std::vector<uint8_t> frame;
        frame.reserve(FIXED_HEADER_SIZE + extLength + payload.size());

        WriteFixedHeader(frame, MsgType::VIDEO, flags, timestampMs,
                         extLength, static_cast<uint32_t>(payload.size()));
        WriteCommonExtHeader(frame, absTimeMs);
        WriteVideoExtHeader(frame, codec, frameType);
        frame.insert(frame.end(), payload.begin(), payload.end());

        frames.push_back(std::move(frame));
    } else {
        // Fragmented: split payload into chunks of FRAGMENT_THRESHOLD
        uint16_t totalFragments = static_cast<uint16_t>(
            (payload.size() + FRAGMENT_THRESHOLD - 1) / FRAGMENT_THRESHOLD);

        for (uint16_t i = 0; i < totalFragments; ++i) {
            size_t offset = static_cast<size_t>(i) * FRAGMENT_THRESHOLD;
            size_t chunkSize = payload.size() - offset;
            if (chunkSize > FRAGMENT_THRESHOLD) {
                chunkSize = FRAGMENT_THRESHOLD;
            }

            std::vector<uint8_t> frame;
            uint8_t flags = FLAG_FRAGMENT;
            uint8_t extLength;

            if (i == 0) {
                // First fragment: frag ext + common ext + video ext
                extLength = kFragExtSize + kCommonExtSize + kVideoExtSize;
                flags |= FLAG_HAS_COMMON;

                frame.reserve(FIXED_HEADER_SIZE + extLength + chunkSize);
                WriteFixedHeader(frame, MsgType::VIDEO, flags, timestampMs,
                                 extLength, static_cast<uint32_t>(chunkSize));
                WriteFragmentExtHeader(frame, frameId, i, totalFragments);
                WriteCommonExtHeader(frame, absTimeMs);
                WriteVideoExtHeader(frame, codec, frameType);
            } else {
                // Subsequent fragments: frag ext only
                extLength = kFragExtSize;

                frame.reserve(FIXED_HEADER_SIZE + extLength + chunkSize);
                WriteFixedHeader(frame, MsgType::VIDEO, flags, timestampMs,
                                 extLength, static_cast<uint32_t>(chunkSize));
                WriteFragmentExtHeader(frame, frameId, i, totalFragments);
            }

            frame.insert(frame.end(), payload.begin() + offset,
                         payload.begin() + offset + chunkSize);

            frames.push_back(std::move(frame));
        }
    }

    return frames;
}

void FrameProtocol::WriteAudioExtHeader(std::vector<uint8_t>& buf,
                                        AudioCodec codec,
                                        SampleRateCode sampleRate,
                                        uint8_t channels) {
    buf.push_back(static_cast<uint8_t>(codec));
    buf.push_back(static_cast<uint8_t>(sampleRate));
    buf.push_back(channels);
    buf.push_back(0);  // reserved
}

SampleRateCode FrameProtocol::SampleRateToCode(int32_t sampleRate) {
    switch (sampleRate) {
        case 8000:  return SampleRateCode::RATE_8000;
        case 16000: return SampleRateCode::RATE_16000;
        case 44100: return SampleRateCode::RATE_44100;
        case 48000: return SampleRateCode::RATE_48000;
        default:    return SampleRateCode::RATE_8000;
    }
}

std::vector<std::vector<uint8_t>> FrameProtocol::EncodeAudioFrame(
    const std::vector<uint8_t>& payload,
    AudioCodec codec,
    SampleRateCode sampleRate,
    uint8_t channels,
    int64_t timestampMs,
    int64_t absTimeMs,
    uint16_t frameId) {

    std::vector<std::vector<uint8_t>> frames;

    const uint8_t kCommonExtSize = 10;
    const uint8_t kAudioExtSize = 4;   // codec(1) + sample_rate(1) + channels(1) + reserved(1)
    const uint8_t kFragExtSize = 6;

    if (payload.size() <= FRAGMENT_THRESHOLD) {
        uint8_t extLength = kCommonExtSize + kAudioExtSize;
        uint8_t flags = FLAG_HAS_COMMON;

        std::vector<uint8_t> frame;
        frame.reserve(FIXED_HEADER_SIZE + extLength + payload.size());

        WriteFixedHeader(frame, MsgType::AUDIO, flags, timestampMs,
                         extLength, static_cast<uint32_t>(payload.size()));
        WriteCommonExtHeader(frame, absTimeMs);
        WriteAudioExtHeader(frame, codec, sampleRate, channels);
        frame.insert(frame.end(), payload.begin(), payload.end());

        frames.push_back(std::move(frame));
    } else {
        uint16_t totalFragments = static_cast<uint16_t>(
            (payload.size() + FRAGMENT_THRESHOLD - 1) / FRAGMENT_THRESHOLD);

        for (uint16_t i = 0; i < totalFragments; ++i) {
            size_t offset = static_cast<size_t>(i) * FRAGMENT_THRESHOLD;
            size_t chunkSize = payload.size() - offset;
            if (chunkSize > FRAGMENT_THRESHOLD) {
                chunkSize = FRAGMENT_THRESHOLD;
            }

            std::vector<uint8_t> frame;
            uint8_t flags = FLAG_FRAGMENT;
            uint8_t extLength;

            if (i == 0) {
                extLength = kFragExtSize + kCommonExtSize + kAudioExtSize;
                flags |= FLAG_HAS_COMMON;

                frame.reserve(FIXED_HEADER_SIZE + extLength + chunkSize);
                WriteFixedHeader(frame, MsgType::AUDIO, flags, timestampMs,
                                 extLength, static_cast<uint32_t>(chunkSize));
                WriteFragmentExtHeader(frame, frameId, i, totalFragments);
                WriteCommonExtHeader(frame, absTimeMs);
                WriteAudioExtHeader(frame, codec, sampleRate, channels);
            } else {
                extLength = kFragExtSize;

                frame.reserve(FIXED_HEADER_SIZE + extLength + chunkSize);
                WriteFixedHeader(frame, MsgType::AUDIO, flags, timestampMs,
                                 extLength, static_cast<uint32_t>(chunkSize));
                WriteFragmentExtHeader(frame, frameId, i, totalFragments);
            }

            frame.insert(frame.end(), payload.begin() + offset,
                         payload.begin() + offset + chunkSize);

            frames.push_back(std::move(frame));
        }
    }

    return frames;
}

}  // namespace server
