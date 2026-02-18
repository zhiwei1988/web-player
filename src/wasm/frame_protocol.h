#ifndef FRAME_PROTOCOL_WASM_H
#define FRAME_PROTOCOL_WASM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FRAME_COMPLETE          = 0,
    FRAME_FRAGMENT_PENDING  = 1,
    FRAME_ERROR             = -1,
    FRAME_SKIP              = 2
} FrameParseStatus;

typedef struct {
    uint8_t  msg_type;
    uint8_t  video_codec;
    uint8_t  video_frame_type;
    uint16_t video_resolution;
    int64_t  timestamp;       /* relative timestamp from fixed header */
    int64_t  abs_time;        /* absolute UTC ms from common ext header */
    uint8_t* payload;         /* pointer to reassembled payload (WASM heap) */
    uint32_t payload_size;
} ParsedFrame;

/**
 * Initialize protocol parser (allocate fragment buffers etc.)
 * @return 0 on success, negative on error
 */
int frame_protocol_init(void);

/**
 * Parse a single protocol frame.
 * For fragmented frames, returns FRAME_FRAGMENT_PENDING until all fragments
 * are received, then returns FRAME_COMPLETE with reassembled payload.
 *
 * @param data    raw protocol frame data
 * @param size    data length
 * @param result  output parsed frame info
 * @return FrameParseStatus
 */
FrameParseStatus frame_protocol_parse(const uint8_t* data, int size,
                                      ParsedFrame* result);

/**
 * Destroy parser and free all internal buffers
 */
void frame_protocol_destroy(void);

/**
 * Allocate a ParsedFrame struct on WASM heap
 */
ParsedFrame* frame_protocol_alloc_result(void);

/**
 * Free a ParsedFrame struct (and its payload if any)
 */
void frame_protocol_free_result(ParsedFrame* result);

#ifdef __cplusplus
}
#endif

#endif /* FRAME_PROTOCOL_WASM_H */
