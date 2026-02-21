#ifndef MP4_DEMUXER_H
#define MP4_DEMUXER_H

#include <cstdint>
#include <string>
#include <vector>

namespace server {

enum class MediaType : uint8_t {
    VIDEO = 0,
    AUDIO = 1
};

/**
 * @brief A single media packet extracted from MP4 container
 */
struct MediaPacket {
    MediaType type;
    std::vector<uint8_t> data;
    int64_t ptsMs;  // presentation timestamp in milliseconds
};

/**
 * @brief Audio stream metadata
 */
struct AudioInfo {
    std::string codecName;   // "aac", "pcm_alaw", "pcm_mulaw", "g726"
    int32_t sampleRate;
    int32_t channels;
    bool present;
};

/**
 * @brief Video stream metadata
 */
struct VideoInfo {
    std::string codecName;   // "h264", "h265"
    double frameRate;
    bool isH265;
    bool present;
};

/**
 * @brief MP4 container demuxer using libavformat
 */
class Mp4Demuxer {
public:
    Mp4Demuxer();
    ~Mp4Demuxer();

    // Non-copyable
    Mp4Demuxer(const Mp4Demuxer&) = delete;
    Mp4Demuxer& operator=(const Mp4Demuxer&) = delete;

    /**
     * @brief Open MP4 file and extract all packets
     * @param filePath path to MP4 file
     * @return true on success
     */
    bool LoadFile(const std::string& filePath);

    /**
     * @brief Get total number of packets (audio + video)
     */
    size_t GetPacketCount() const { return packets_.size(); }

    /**
     * @brief Get packet by index
     * @return pointer to packet, nullptr if out of range
     */
    const MediaPacket* GetPacket(size_t index) const;

    /**
     * @brief Get video stream metadata
     */
    const VideoInfo& GetVideoInfo() const { return videoInfo_; }

    /**
     * @brief Get audio stream metadata
     */
    const AudioInfo& GetAudioInfo() const { return audioInfo_; }

    /**
     * @brief Get detected video frame rate
     * @return fps, or 25.0 if not detected
     */
    double GetFrameRate() const;

private:
    std::vector<MediaPacket> packets_;
    VideoInfo videoInfo_;
    AudioInfo audioInfo_;
};

}  // namespace server

#endif  // MP4_DEMUXER_H
