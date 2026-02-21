#include "mp4_demuxer.h"

#include <algorithm>
#include <cstdio>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
}

namespace server {

Mp4Demuxer::Mp4Demuxer()
    : videoInfo_{"", 0.0, false, false},
      audioInfo_{"", 0, 0, false} {
}

Mp4Demuxer::~Mp4Demuxer() {
}

bool Mp4Demuxer::LoadFile(const std::string& filePath) {
    AVFormatContext* fmtCtx = nullptr;

    if (avformat_open_input(&fmtCtx, filePath.c_str(), nullptr, nullptr) < 0) {
        std::fprintf(stderr, "Failed to open file: %s\n", filePath.c_str());
        return false;
    }

    if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
        std::fprintf(stderr, "Failed to find stream info: %s\n", filePath.c_str());
        avformat_close_input(&fmtCtx);
        return false;
    }

    int32_t videoStreamIndex = -1;
    int32_t audioStreamIndex = -1;

    for (uint32_t i = 0; i < fmtCtx->nb_streams; ++i) {
        AVStream* stream = fmtCtx->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && videoStreamIndex < 0) {
            videoStreamIndex = static_cast<int32_t>(i);
        } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audioStreamIndex < 0) {
            audioStreamIndex = static_cast<int32_t>(i);
        }
    }

    if (videoStreamIndex < 0) {
        std::fprintf(stderr, "No video stream found in: %s\n", filePath.c_str());
        avformat_close_input(&fmtCtx);
        return false;
    }

    // Extract video metadata
    AVStream* videoStream = fmtCtx->streams[videoStreamIndex];
    AVCodecID videoCodecId = videoStream->codecpar->codec_id;

    videoInfo_.present = true;
    videoInfo_.isH265 = (videoCodecId == AV_CODEC_ID_HEVC);
    videoInfo_.codecName = videoInfo_.isH265 ? "h265" : "h264";

    if (videoStream->avg_frame_rate.den > 0) {
        videoInfo_.frameRate = av_q2d(videoStream->avg_frame_rate);
    } else if (videoStream->r_frame_rate.den > 0) {
        videoInfo_.frameRate = av_q2d(videoStream->r_frame_rate);
    } else {
        videoInfo_.frameRate = 25.0;
    }

    std::printf("Video: %s, %.2f fps\n", videoInfo_.codecName.c_str(), videoInfo_.frameRate);

    // Create bitstream filter to convert AVCC -> Annex B
    const char* bsfName = videoInfo_.isH265 ? "hevc_mp4toannexb" : "h264_mp4toannexb";
    const AVBitStreamFilter* bsf = av_bsf_get_by_name(bsfName);
    AVBSFContext* bsfCtx = nullptr;

    if (bsf == nullptr || av_bsf_alloc(bsf, &bsfCtx) < 0) {
        std::fprintf(stderr, "Failed to create BSF: %s\n", bsfName);
        avformat_close_input(&fmtCtx);
        return false;
    }

    if (avcodec_parameters_copy(bsfCtx->par_in, videoStream->codecpar) < 0 ||
        av_bsf_init(bsfCtx) < 0) {
        std::fprintf(stderr, "Failed to init BSF: %s\n", bsfName);
        av_bsf_free(&bsfCtx);
        avformat_close_input(&fmtCtx);
        return false;
    }

    // Extract audio metadata
    if (audioStreamIndex >= 0) {
        AVStream* audioStream = fmtCtx->streams[audioStreamIndex];
        AVCodecID audioCodecId = audioStream->codecpar->codec_id;

        bool isSupportedAudio = (audioCodecId == AV_CODEC_ID_AAC ||
                                 audioCodecId == AV_CODEC_ID_PCM_ALAW ||
                                 audioCodecId == AV_CODEC_ID_PCM_MULAW ||
                                 audioCodecId == AV_CODEC_ID_ADPCM_G726);

        if (isSupportedAudio) {
            audioInfo_.present = true;
            audioInfo_.sampleRate = audioStream->codecpar->sample_rate;
            audioInfo_.channels = audioStream->codecpar->ch_layout.nb_channels;

            if (audioCodecId == AV_CODEC_ID_AAC) {
                audioInfo_.codecName = "aac";
            } else if (audioCodecId == AV_CODEC_ID_PCM_ALAW) {
                audioInfo_.codecName = "pcm_alaw";
            } else if (audioCodecId == AV_CODEC_ID_PCM_MULAW) {
                audioInfo_.codecName = "pcm_mulaw";
            } else if (audioCodecId == AV_CODEC_ID_ADPCM_G726) {
                audioInfo_.codecName = "g726";
            }

            std::printf("Audio: %s, %d Hz, %d ch\n",
                        audioInfo_.codecName.c_str(),
                        audioInfo_.sampleRate,
                        audioInfo_.channels);
        } else {
            std::fprintf(stderr, "Unsupported audio codec, ignoring audio track\n");
        }
    }

    // Read all packets
    AVPacket* pkt = av_packet_alloc();
    if (pkt == nullptr) {
        avformat_close_input(&fmtCtx);
        return false;
    }

    while (av_read_frame(fmtCtx, pkt) >= 0) {
        if (pkt->stream_index != videoStreamIndex &&
            pkt->stream_index != audioStreamIndex) {
            av_packet_unref(pkt);
            continue;
        }

        // Skip audio packets if audio is not supported
        if (pkt->stream_index == audioStreamIndex && !audioInfo_.present) {
            av_packet_unref(pkt);
            continue;
        }

        AVStream* stream = fmtCtx->streams[pkt->stream_index];
        bool isVideo = (pkt->stream_index == videoStreamIndex);

        // Apply BSF to convert video packets from AVCC to Annex B
        if (isVideo) {
            if (av_bsf_send_packet(bsfCtx, pkt) < 0) {
                av_packet_unref(pkt);
                continue;
            }
            while (av_bsf_receive_packet(bsfCtx, pkt) == 0) {
                MediaPacket mediaPkt;
                mediaPkt.type = MediaType::VIDEO;
                mediaPkt.data.assign(pkt->data, pkt->data + pkt->size);
                if (pkt->pts != AV_NOPTS_VALUE) {
                    mediaPkt.ptsMs = av_rescale_q(pkt->pts, stream->time_base,
                                                  AVRational{1, 1000});
                } else {
                    mediaPkt.ptsMs = 0;
                }
                packets_.push_back(std::move(mediaPkt));
                av_packet_unref(pkt);
            }
        } else {
            MediaPacket mediaPkt;
            mediaPkt.type = MediaType::AUDIO;
            mediaPkt.data.assign(pkt->data, pkt->data + pkt->size);
            if (pkt->pts != AV_NOPTS_VALUE) {
                mediaPkt.ptsMs = av_rescale_q(pkt->pts, stream->time_base,
                                              AVRational{1, 1000});
            } else {
                mediaPkt.ptsMs = 0;
            }
            packets_.push_back(std::move(mediaPkt));
            av_packet_unref(pkt);
        }
    }

    av_bsf_free(&bsfCtx);
    av_packet_free(&pkt);
    avformat_close_input(&fmtCtx);

    // Sort by PTS for interleaved sending
    std::sort(packets_.begin(), packets_.end(),
              [](const MediaPacket& a, const MediaPacket& b) {
                  return a.ptsMs < b.ptsMs;
              });

    std::printf("Loaded %zu packets (%s)\n", packets_.size(), filePath.c_str());

    return true;
}

const MediaPacket* Mp4Demuxer::GetPacket(size_t index) const {
    if (index >= packets_.size()) {
        return nullptr;
    }
    return &packets_[index];
}

double Mp4Demuxer::GetFrameRate() const {
    if (videoInfo_.present && videoInfo_.frameRate > 0.0) {
        return videoInfo_.frameRate;
    }
    return 25.0;
}

}  // namespace server
