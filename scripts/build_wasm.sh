#!/bin/bash
# WASM decoder module build script - M1 phase
set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FFMPEG_DIR="${PROJECT_ROOT}/ffmpeg-build"
SRC_DIR="${PROJECT_ROOT}/src/wasm"
DIST_DIR="${PROJECT_ROOT}/dist"

export EMSDK="/home/zhiwei/OpenSource/emsdk"
source "${EMSDK}/emsdk_env.sh"

# ===========================
# Check dependencies
# ===========================
echo "Checking build dependencies..."
if [ ! -d "${FFMPEG_DIR}" ]; then
    echo "Error: FFmpeg not built, please run: npm run build:ffmpeg"
    exit 1
fi

if [ ! -f "${SRC_DIR}/decoder_wasm.c" ]; then
    echo "Error: Source file not found: ${SRC_DIR}/decoder_wasm.c"
    exit 1
fi

echo "FFmpeg: ${FFMPEG_DIR}"
echo "Source file: ${SRC_DIR}/decoder_wasm.c"

mkdir -p "${DIST_DIR}"

# ===========================
# Build parameters
# ===========================
CC=emcc

CFLAGS=(
    -O3
    -I"${FFMPEG_DIR}/include"
    -fno-exceptions
)

LDFLAGS=(
    -L"${FFMPEG_DIR}/lib"
    -lavformat
    -lavcodec
    -lavutil
    -pthread
)

EMFLAGS=(
    -s WASM=1
    -s WASM_BIGINT=1
    -s MODULARIZE=1
    -s EXPORT_ES6=1
    -s EXPORT_NAME="createDecoderModule"
    -s ALLOW_MEMORY_GROWTH=1
    -s INITIAL_MEMORY=67108864
    -s MAXIMUM_MEMORY=536870912
    -s STACK_SIZE=1048576
    -s NO_EXIT_RUNTIME=1
    -s FILESYSTEM=0
    -s ENVIRONMENT='web,worker'
    -s STANDALONE_WASM=0
    -sASSERTIONS=1
    -s INCOMING_MODULE_JS_API=locateFile
    -s EXPORTED_FUNCTIONS='[
        "_decoder_init_video",
        "_decoder_send_video_packet",
        "_decoder_receive_video_frame",
        "_decoder_flush_video",
        "_decoder_destroy",
        "_decoder_get_version",
        "_decoder_get_ffmpeg_version",
        "_decoder_malloc",
        "_decoder_free",
        "_frame_protocol_init",
        "_frame_protocol_parse",
        "_frame_protocol_destroy",
        "_frame_protocol_alloc_result",
        "_frame_protocol_free_result"
    ]'
    -s EXPORTED_RUNTIME_METHODS='[
        "ccall",
        "cwrap",
        "getValue",
        "setValue",
        "HEAPU8",
        "HEAPF32"
    ]'
)

# ===========================
# Build
# ===========================
echo ""
echo "Building WASM module..."
echo "   Start time: $(date '+%H:%M:%S')"

${CC} \
    "${CFLAGS[@]}" \
    "${SRC_DIR}/decoder_wasm.c" \
    "${SRC_DIR}/frame_protocol.c" \
    "${LDFLAGS[@]}" \
    "${EMFLAGS[@]}" \
    -o "${DIST_DIR}/decoder.js"

echo "   End time: $(date '+%H:%M:%S')"
echo "WASM build completed"

# ===========================
# Verify build artifacts
# ===========================
echo ""
echo "Verifying build artifacts..."
for file in "decoder.js" "decoder.wasm"; do
    FILEPATH="${DIST_DIR}/${file}"
    if [ -f "${FILEPATH}" ]; then
        SIZE=$(du -h "${FILEPATH}" | cut -f1)
        echo "  ${file} (${SIZE})"
    else
        echo "  Missing: ${file}"
        exit 1
    fi
done

# ===========================
# Size analysis
# ===========================
echo ""
echo "Size analysis:"
WASM_SIZE=$(stat -c%s "${DIST_DIR}/decoder.wasm" 2>/dev/null || stat -f%z "${DIST_DIR}/decoder.wasm")
WASM_SIZE_MB=$(echo "scale=2; ${WASM_SIZE} / 1024 / 1024" | bc)
echo "  WASM uncompressed: ${WASM_SIZE_MB} MB"

# Gzip compression test
if command -v gzip &> /dev/null; then
    gzip -c "${DIST_DIR}/decoder.wasm" > "${DIST_DIR}/decoder.wasm.gz"
    GZIP_SIZE=$(stat -c%s "${DIST_DIR}/decoder.wasm.gz" 2>/dev/null || stat -f%z "${DIST_DIR}/decoder.wasm.gz")
    GZIP_SIZE_MB=$(echo "scale=2; ${GZIP_SIZE} / 1024 / 1024" | bc)
    echo "  WASM Gzip compressed: ${GZIP_SIZE_MB} MB"
    rm "${DIST_DIR}/decoder.wasm.gz"

    # Check if size meets requirements (≤ 5MB gzip)
    if (( $(echo "${GZIP_SIZE_MB} > 5.0" | bc -l) )); then
        echo "  Warning: Size exceeds limit (target ≤ 5MB)"
    else
        echo "  Size meets requirements"
    fi
fi

echo ""
echo "WASM module build successful!"
echo "   Output directory: ${DIST_DIR}"
