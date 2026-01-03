/**
 * decoder_wasm.h - FFmpeg WASM decoder C interface definition
 * M1 stage: H.264 video decoding only
 */

#ifndef DECODER_WASM_H
#define DECODER_WASM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Codec type enumeration */
typedef enum {
    CODEC_H264 = 0,
    CODEC_H265 = 1,  // M2 stage
    CODEC_AAC  = 2,  // M2 stage
    CODEC_OPUS = 3   // M2 stage
} CodecType;

/* Decode result status */
typedef enum {
    DECODE_OK = 0,
    DECODE_NEED_MORE_DATA = 1,
    DECODE_ERROR = -1,
    DECODE_EOF = -2
} DecodeStatus;

/* Video frame information structure */
typedef struct {
    int width;
    int height;
    int64_t pts;
    int64_t duration;

    // YUV420P data pointers
    uint8_t* y_data;
    uint8_t* u_data;
    uint8_t* v_data;

    // Strides
    int y_stride;
    int u_stride;
    int v_stride;
} VideoFrameInfo;

/* Audio frame information structure (M2 stage) */
typedef struct {
    int sample_rate;
    int channels;
    int nb_samples;
    int64_t pts;
    float* data;  // Interleaved PCM data
} AudioFrameInfo;

// ===========================
// Core API
// ===========================

/**
 * Initialize video decoder
 * @param codec_type Codec type
 * @return 0 on success, negative value on failure
 */
int decoder_init_video(CodecType codec_type);

/**
 * Send encoded data to video decoder
 * @param data Encoded data pointer
 * @param size Data length
 * @param pts Presentation timestamp
 * @return DecodeStatus
 */
DecodeStatus decoder_send_video_packet(const uint8_t* data, int size, int64_t pts);

/**
 * Receive decoded video frame
 * @param frame_info Output frame information structure pointer
 * @return DecodeStatus
 */
DecodeStatus decoder_receive_video_frame(VideoFrameInfo* frame_info);

/**
 * Flush video decoder buffer
 */
void decoder_flush_video(void);

/**
 * Destroy decoder and release resources
 */
void decoder_destroy(void);

/**
 * Get decoder version information
 * @return Version string
 */
const char* decoder_get_version(void);

/**
 * Get FFmpeg version information
 * @return FFmpeg version string
 */
const char* decoder_get_ffmpeg_version(void);

/**
 * Memory allocation function (for JS calls)
 * @param size Allocation size
 * @return Memory pointer
 */
void* decoder_malloc(int size);

/**
 * Memory deallocation function (for JS calls)
 * @param ptr Memory pointer
 */
void decoder_free(void* ptr);

#ifdef __cplusplus
}
#endif

#endif /* DECODER_WASM_H */
