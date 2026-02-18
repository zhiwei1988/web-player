#include "frame_protocol.h"

#include <stdlib.h>
#include <string.h>

/* Protocol constants */
#define PROTOCOL_MAGIC       0xEB01
#define PROTOCOL_VERSION     1
#define FIXED_HEADER_SIZE    20
#define FRAGMENT_EXT_SIZE    6
#define MAX_FRAGMENTS        256
#define FRAGMENT_TIMEOUT_MS  500

/* flags bit definitions */
#define FLAG_FRAGMENT    0x01
#define FLAG_HAS_COMMON  0x08

/* common_flags bit definitions */
#define COMMON_ABS_TIME  0x01

/* Fragment reassembly entry */
typedef struct {
    uint16_t frame_id;
    uint16_t total_fragments;
    uint16_t received_count;
    uint32_t total_payload_size;

    /* Per-fragment storage */
    uint8_t** fragment_data;
    uint32_t* fragment_sizes;
    uint8_t*  fragment_present;

    /* Metadata from first fragment */
    uint8_t  msg_type;
    uint8_t  video_codec;
    uint8_t  video_frame_type;
    uint16_t video_resolution;
    int64_t  timestamp;
    int64_t  abs_time;

    uint8_t  active;
} FragmentEntry;

/* Parser state */
static FragmentEntry* g_fragments = NULL;
static int g_max_entries = 16;
static int g_initialized = 0;

/* Big-endian read helpers */
static uint16_t read_be16(const uint8_t* p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t read_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static int64_t read_be64(const uint8_t* p) {
    uint64_t val = 0;
    for (int i = 0; i < 8; ++i) {
        val = (val << 8) | p[i];
    }
    return (int64_t)val;
}

static void free_fragment_entry(FragmentEntry* entry) {
    if (entry->fragment_data != NULL) {
        for (uint16_t i = 0; i < entry->total_fragments; ++i) {
            if (entry->fragment_data[i] != NULL) {
                free(entry->fragment_data[i]);
            }
        }
        free(entry->fragment_data);
        entry->fragment_data = NULL;
    }
    if (entry->fragment_sizes != NULL) {
        free(entry->fragment_sizes);
        entry->fragment_sizes = NULL;
    }
    if (entry->fragment_present != NULL) {
        free(entry->fragment_present);
        entry->fragment_present = NULL;
    }
    entry->active = 0;
}

static FragmentEntry* find_fragment_entry(uint16_t frame_id) {
    for (int i = 0; i < g_max_entries; ++i) {
        if (g_fragments[i].active && g_fragments[i].frame_id == frame_id) {
            return &g_fragments[i];
        }
    }
    return NULL;
}

static FragmentEntry* alloc_fragment_entry(uint16_t frame_id,
                                           uint16_t total_fragments) {
    /* Find a free slot */
    FragmentEntry* entry = NULL;
    for (int i = 0; i < g_max_entries; ++i) {
        if (!g_fragments[i].active) {
            entry = &g_fragments[i];
            break;
        }
    }

    /* If no free slot, evict the first one (oldest) */
    if (entry == NULL) {
        free_fragment_entry(&g_fragments[0]);
        entry = &g_fragments[0];
    }

    memset(entry, 0, sizeof(FragmentEntry));
    entry->frame_id = frame_id;
    entry->total_fragments = total_fragments;
    entry->active = 1;

    if (total_fragments > 0 && total_fragments <= MAX_FRAGMENTS) {
        entry->fragment_data = (uint8_t**)calloc(total_fragments, sizeof(uint8_t*));
        entry->fragment_sizes = (uint32_t*)calloc(total_fragments, sizeof(uint32_t));
        entry->fragment_present = (uint8_t*)calloc(total_fragments, sizeof(uint8_t));
    }

    return entry;
}

static uint8_t* reassemble_fragments(FragmentEntry* entry, uint32_t* out_size) {
    uint32_t total = 0;
    for (uint16_t i = 0; i < entry->total_fragments; ++i) {
        total += entry->fragment_sizes[i];
    }

    uint8_t* buf = (uint8_t*)malloc(total);
    if (buf == NULL) {
        *out_size = 0;
        return NULL;
    }

    uint32_t offset = 0;
    for (uint16_t i = 0; i < entry->total_fragments; ++i) {
        memcpy(buf + offset, entry->fragment_data[i], entry->fragment_sizes[i]);
        offset += entry->fragment_sizes[i];
    }

    *out_size = total;
    return buf;
}

/* Parse extension headers starting at ext_data with given flags */
static void parse_ext_headers(const uint8_t* ext_data, uint8_t ext_length,
                              uint8_t flags, uint8_t msg_type,
                              uint16_t* out_frame_id,
                              uint16_t* out_frag_index,
                              uint16_t* out_total_frags,
                              int64_t* out_abs_time,
                              uint8_t* out_video_codec,
                              uint8_t* out_video_frame_type,
                              uint16_t* out_video_resolution) {
    uint8_t offset = 0;

    /* Layer 1: Fragment ext header (6B) */
    if ((flags & FLAG_FRAGMENT) && offset + FRAGMENT_EXT_SIZE <= ext_length) {
        *out_frame_id = read_be16(ext_data + offset);
        *out_frag_index = read_be16(ext_data + offset + 2);
        *out_total_frags = read_be16(ext_data + offset + 4);
        offset += FRAGMENT_EXT_SIZE;
    }

    /* Layer 2: Common ext header (variable, self-describing length) */
    if ((flags & FLAG_HAS_COMMON) && offset + 2 <= ext_length) {
        uint8_t common_length = ext_data[offset];
        uint8_t common_flags = ext_data[offset + 1];

        uint8_t field_offset = 2;  /* past common_length and common_flags */

        if ((common_flags & COMMON_ABS_TIME) &&
            field_offset + 8 <= common_length && offset + field_offset + 8 <= ext_length) {
            *out_abs_time = read_be64(ext_data + offset + field_offset);
            field_offset += 8;
        }

        /* Skip rest of common ext by common_length */
        offset += common_length;
    }

    /* Layer 3: Type-specific ext header */
    if (msg_type == 0x01 && offset + 4 <= ext_length) {
        /* Video ext header: codec(1) + frame_type(1) + resolution(2) */
        *out_video_codec = ext_data[offset];
        *out_video_frame_type = ext_data[offset + 1];
        *out_video_resolution = read_be16(ext_data + offset + 2);
    }
}

int frame_protocol_init(void) {
    if (g_initialized) {
        return 0;
    }

    g_fragments = (FragmentEntry*)calloc(g_max_entries, sizeof(FragmentEntry));
    if (g_fragments == NULL) {
        return -1;
    }

    g_initialized = 1;
    return 0;
}

FrameParseStatus frame_protocol_parse(const uint8_t* data, int size,
                                      ParsedFrame* result) {
    if (result == NULL || data == NULL) {
        return FRAME_ERROR;
    }

    /* Need at least fixed header */
    if (size < FIXED_HEADER_SIZE) {
        return FRAME_ERROR;
    }

    /* Validate magic */
    uint16_t magic = read_be16(data);
    if (magic != PROTOCOL_MAGIC) {
        return FRAME_ERROR;
    }

    /* Validate version */
    uint8_t version = data[2];
    if (version != PROTOCOL_VERSION) {
        return FRAME_SKIP;
    }

    uint8_t msg_type = data[3];
    uint8_t flags = data[4];
    int64_t timestamp = read_be64(data + 5);
    uint8_t ext_length = data[13];
    uint32_t payload_length = read_be32(data + 14);
    /* reserved at data[18..19] */

    /* Validate total size */
    if (size < FIXED_HEADER_SIZE + ext_length + (int)payload_length) {
        return FRAME_ERROR;
    }

    const uint8_t* ext_data = data + FIXED_HEADER_SIZE;
    const uint8_t* payload_data = data + FIXED_HEADER_SIZE + ext_length;

    /* Parse extension headers */
    uint16_t frame_id = 0;
    uint16_t frag_index = 0;
    uint16_t total_frags = 0;
    int64_t abs_time = 0;
    uint8_t video_codec = 0;
    uint8_t video_frame_type = 0;
    uint16_t video_resolution = 0;

    parse_ext_headers(ext_data, ext_length, flags, msg_type,
                      &frame_id, &frag_index, &total_frags,
                      &abs_time, &video_codec, &video_frame_type,
                      &video_resolution);

    /* Non-fragmented frame */
    if (!(flags & FLAG_FRAGMENT)) {
        /* Free previous payload if any */
        if (result->payload != NULL) {
            free(result->payload);
            result->payload = NULL;
        }

        result->msg_type = msg_type;
        result->timestamp = timestamp;
        result->abs_time = abs_time;
        result->video_codec = video_codec;
        result->video_frame_type = video_frame_type;
        result->video_resolution = video_resolution;
        result->payload_size = payload_length;

        if (payload_length > 0) {
            result->payload = (uint8_t*)malloc(payload_length);
            if (result->payload == NULL) {
                return FRAME_ERROR;
            }
            memcpy(result->payload, payload_data, payload_length);
        }

        return FRAME_COMPLETE;
    }

    /* Fragmented frame */
    if (total_frags == 0 || total_frags > MAX_FRAGMENTS) {
        return FRAME_ERROR;
    }

    FragmentEntry* entry = find_fragment_entry(frame_id);

    if (entry == NULL) {
        entry = alloc_fragment_entry(frame_id, total_frags);
        if (entry == NULL || entry->fragment_data == NULL) {
            return FRAME_ERROR;
        }
    }

    /* Store first fragment metadata */
    if (frag_index == 0) {
        entry->msg_type = msg_type;
        entry->timestamp = timestamp;
        entry->abs_time = abs_time;
        entry->video_codec = video_codec;
        entry->video_frame_type = video_frame_type;
        entry->video_resolution = video_resolution;
    }

    /* Store this fragment's payload */
    if (frag_index < entry->total_fragments && !entry->fragment_present[frag_index]) {
        entry->fragment_data[frag_index] = (uint8_t*)malloc(payload_length);
        if (entry->fragment_data[frag_index] == NULL) {
            return FRAME_ERROR;
        }
        memcpy(entry->fragment_data[frag_index], payload_data, payload_length);
        entry->fragment_sizes[frag_index] = payload_length;
        entry->fragment_present[frag_index] = 1;
        entry->received_count++;
    }

    /* Check if all fragments received */
    if (entry->received_count >= entry->total_fragments) {
        /* Reassemble */
        uint32_t reassembled_size = 0;
        uint8_t* reassembled = reassemble_fragments(entry, &reassembled_size);

        if (reassembled == NULL) {
            free_fragment_entry(entry);
            return FRAME_ERROR;
        }

        /* Free previous payload if any */
        if (result->payload != NULL) {
            free(result->payload);
        }

        result->msg_type = entry->msg_type;
        result->timestamp = entry->timestamp;
        result->abs_time = entry->abs_time;
        result->video_codec = entry->video_codec;
        result->video_frame_type = entry->video_frame_type;
        result->video_resolution = entry->video_resolution;
        result->payload = reassembled;
        result->payload_size = reassembled_size;

        free_fragment_entry(entry);
        return FRAME_COMPLETE;
    }

    return FRAME_FRAGMENT_PENDING;
}

void frame_protocol_destroy(void) {
    if (g_fragments != NULL) {
        for (int i = 0; i < g_max_entries; ++i) {
            if (g_fragments[i].active) {
                free_fragment_entry(&g_fragments[i]);
            }
        }
        free(g_fragments);
        g_fragments = NULL;
    }
    g_initialized = 0;
}

ParsedFrame* frame_protocol_alloc_result(void) {
    ParsedFrame* result = (ParsedFrame*)calloc(1, sizeof(ParsedFrame));
    return result;
}

void frame_protocol_free_result(ParsedFrame* result) {
    if (result != NULL) {
        if (result->payload != NULL) {
            free(result->payload);
        }
        free(result);
    }
}
