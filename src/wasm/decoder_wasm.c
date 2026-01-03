/**
 * decoder_wasm.c - FFmpeg WASM decoder implementation
 * M1 stage: H.264 video decoding only
 */

#include "decoder_wasm.h"
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavutil/version.h>
#include <stdio.h>
#include <string.h>

#define DECODER_VERSION "1.0.0-M1"

// ===========================
// Global state
// ===========================
static AVCodecContext* g_video_ctx = NULL;
static AVFrame* g_video_frame = NULL;
static AVPacket* g_video_packet = NULL;
static int g_initialized = 0;

// ===========================
// Internal helper functions
// ===========================

/**
 * Find decoder by codec type
 */
static const AVCodec* find_decoder(CodecType codec_type) {
    switch (codec_type) {
        case CODEC_H264:
            return avcodec_find_decoder(AV_CODEC_ID_H264);
        case CODEC_H265:
            return avcodec_find_decoder(AV_CODEC_ID_HEVC);
        case CODEC_AAC:
            return avcodec_find_decoder(AV_CODEC_ID_AAC);
        default:
            return NULL;
    }
}

/**
 * Cleanup video decoder resources
 */
static void cleanup_video_decoder(void) {
    if (g_video_frame) {
        av_frame_free(&g_video_frame);
        g_video_frame = NULL;
    }
    if (g_video_packet) {
        av_packet_free(&g_video_packet);
        g_video_packet = NULL;
    }
    if (g_video_ctx) {
        avcodec_free_context(&g_video_ctx);
        g_video_ctx = NULL;
    }
}

// ===========================
// Public API implementation
// ===========================

int decoder_init_video(CodecType codec_type) {
    // Cleanup if already initialized
    if (g_initialized) {
        cleanup_video_decoder();
        g_initialized = 0;
    }

    // Find decoder
    const AVCodec* codec = find_decoder(codec_type);
    if (!codec) {
        fprintf(stderr, "[decoder] Decoder not found: %d\n", codec_type);
        return -1;
    }

    // Allocate decoder context
    g_video_ctx = avcodec_alloc_context3(codec);
    if (!g_video_ctx) {
        fprintf(stderr, "[decoder] Memory allocation failed: AVCodecContext\n");
        return -2;
    }

    // Open decoder
    if (avcodec_open2(g_video_ctx, codec, NULL) < 0) {
        fprintf(stderr, "[decoder] Failed to open decoder\n");
        avcodec_free_context(&g_video_ctx);
        return -3;
    }

    // Allocate frame and packet
    g_video_frame = av_frame_alloc();
    g_video_packet = av_packet_alloc();

    if (!g_video_frame || !g_video_packet) {
        fprintf(stderr, "[decoder] Memory allocation failed: AVFrame/AVPacket\n");
        cleanup_video_decoder();
        return -4;
    }

    g_initialized = 1;
    printf("[decoder] H.264 decoder initialized successfully\n");

    return 0;
}

DecodeStatus decoder_send_video_packet(const uint8_t* data, int size, int64_t pts) {
    if (!g_initialized || !g_video_ctx || !g_video_packet) {
        fprintf(stderr, "[decoder] Decoder not initialized\n");
        return DECODE_ERROR;
    }

    // Set packet data
    g_video_packet->data = (uint8_t*)data;
    g_video_packet->size = size;
    g_video_packet->pts = pts;
    g_video_packet->dts = pts;

    // Send packet to decoder
    int ret = avcodec_send_packet(g_video_ctx, g_video_packet);

    // Unref packet (don't free data, as it's passed from external)
    av_packet_unref(g_video_packet);

    if (ret < 0) {
        if (ret == AVERROR(EAGAIN)) {
            return DECODE_NEED_MORE_DATA;
        }
        if (ret == AVERROR_EOF) {
            return DECODE_EOF;
        }
        fprintf(stderr, "[decoder] avcodec_send_packet failed: %d\n", ret);
        return DECODE_ERROR;
    }

    return DECODE_OK;
}

DecodeStatus decoder_receive_video_frame(VideoFrameInfo* frame_info) {
    if (!g_initialized || !g_video_ctx || !g_video_frame || !frame_info) {
        fprintf(stderr, "[decoder] Invalid parameters or decoder not initialized\n");
        return DECODE_ERROR;
    }

    // Receive decoded frame
    int ret = avcodec_receive_frame(g_video_ctx, g_video_frame);

    if (ret < 0) {
        if (ret == AVERROR(EAGAIN)) {
            return DECODE_NEED_MORE_DATA;
        }
        if (ret == AVERROR_EOF) {
            return DECODE_EOF;
        }
        fprintf(stderr, "[decoder] avcodec_receive_frame failed: %d\n", ret);
        return DECODE_ERROR;
    }

    // Fill frame information
    frame_info->width = g_video_frame->width;
    frame_info->height = g_video_frame->height;
    frame_info->pts = g_video_frame->pts;
    frame_info->duration = g_video_frame->duration;

    // YUV420P data
    frame_info->y_data = g_video_frame->data[0];
    frame_info->u_data = g_video_frame->data[1];
    frame_info->v_data = g_video_frame->data[2];

    frame_info->y_stride = g_video_frame->linesize[0];
    frame_info->u_stride = g_video_frame->linesize[1];
    frame_info->v_stride = g_video_frame->linesize[2];

    return DECODE_OK;
}

void decoder_flush_video(void) {
    if (g_video_ctx) {
        avcodec_flush_buffers(g_video_ctx);
        printf("[decoder] Video decoder buffer flushed\n");
    }
}

void decoder_destroy(void) {
    cleanup_video_decoder();
    g_initialized = 0;
    printf("[decoder] Decoder destroyed\n");
}

const char* decoder_get_version(void) {
    return DECODER_VERSION;
}

const char* decoder_get_ffmpeg_version(void) {
    return av_version_info();
}

void* decoder_malloc(int size) {
    return av_malloc(size);
}

void decoder_free(void* ptr) {
    av_free(ptr);
}
