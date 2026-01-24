#!/bin/bash
# FFmpeg WASM build script - M1 stage (H.264 decoder only)
set -e

# ===========================
# Configuration parameters
# ===========================
FFMPEG_VERSION="7.1.2"
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
THIRD_PARTY_DIR="${PROJECT_ROOT}/third_party"
FFMPEG_SRC_DIR="${THIRD_PARTY_DIR}/ffmpeg-${FFMPEG_VERSION}"
INSTALL_DIR="${PROJECT_ROOT}/ffmpeg-build"

# Emscripten configuration
export EMSDK="/home/zhiwei/OpenSource/emsdk"
source "${EMSDK}/emsdk_env.sh"

# ===========================
# Check environment
# ===========================
echo "Checking build environment..."
if [ ! -d "${FFMPEG_SRC_DIR}" ]; then
    echo "Error: FFmpeg source code not found, please run: bash build/download_ffmpeg.sh"
    exit 1
fi

which emcc > /dev/null || {
    echo "Error: emcc not found"
    exit 1
}

echo "Emscripten: $(emcc --version | head -1)"
echo "FFmpeg source: ${FFMPEG_SRC_DIR}"
echo "Installation target: ${INSTALL_DIR}"

# ===========================
# Clean old build
# ===========================
echo ""
echo "Cleaning old build..."
rm -rf "${INSTALL_DIR}"
mkdir -p "${INSTALL_DIR}"

cd "${FFMPEG_SRC_DIR}"
make distclean 2>/dev/null || true

# ===========================
# FFmpeg configuration
# ===========================
echo ""
echo "Configuring FFmpeg (M1: minimal H.264 decoder)..."

export CFLAGS="-O3 -fno-exceptions"
export LDFLAGS="-O3"

emconfigure ./configure \
    --prefix="${INSTALL_DIR}" \
    --enable-cross-compile \
    --target-os=none \
    --arch=wasm32 \
    --cpu=generic \
    --cc=emcc \
    --cxx=em++ \
    --ar=emar \
    --ranlib=emranlib \
    --nm=emnm \
    --strip=emstrip \
    \
    --enable-static \
    --disable-shared \
    --disable-programs \
    --disable-doc \
    --disable-htmlpages \
    --disable-manpages \
    --disable-podpages \
    --disable-txtpages \
    \
    --disable-avdevice \
    --disable-swscale \
    --disable-swresample \
    --disable-postproc \
    --disable-avfilter \
    --disable-network \
    --disable-iconv \
    --disable-bzlib \
    --disable-zlib \
    --disable-lzma \
    --disable-sdl2 \
    \
    --disable-vaapi \
    --disable-vdpau \
    --disable-videotoolbox \
    --disable-audiotoolbox \
    --disable-hwaccels \
    \
    --disable-devices \
    --disable-filters \
    --disable-bsfs \
    --disable-muxers \
    --disable-demuxers \
    --disable-parsers \
    --disable-encoders \
    --disable-decoders \
    --disable-protocols \
    \
    --enable-decoder=h264 \
    --enable-decoder=hevc \
    --enable-parser=h264 \
    --enable-parser=hevc \
    --enable-protocol=file \
    \
    --extra-cflags="${CFLAGS}" \
    --extra-ldflags="${LDFLAGS}"

echo "FFmpeg configuration completed"

echo ""
echo "Building FFmpeg ..."

NPROC=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
emmake make -j${NPROC}

echo "FFmpeg build completed"

# ===========================
# Install
# ===========================
echo ""
echo "Installing FFmpeg libraries..."
emmake make install
echo "FFmpeg installation completed: ${INSTALL_DIR}"

# ===========================
# Verify
# ===========================
echo ""
echo "Verifying build artifacts..."
REQUIRED_LIBS=("lib/libavcodec.a" "lib/libavformat.a" "lib/libavutil.a")
ALL_OK=true

for lib in "${REQUIRED_LIBS[@]}"; do
    if [ -f "${INSTALL_DIR}/${lib}" ]; then
        SIZE=$(du -h "${INSTALL_DIR}/${lib}" | cut -f1)
        echo "  ${lib} (${SIZE})"
    else
        echo "  Missing: ${lib}"
        ALL_OK=false
    fi
done

if [ "$ALL_OK" = false ]; then
    echo ""
    echo "Build verification failed, some library files are missing"
    exit 1
fi

echo ""
echo "FFmpeg build successful!"
echo "   Version: ${FFMPEG_VERSION}"
echo "   Installation directory: ${INSTALL_DIR}"
echo ""
echo "Next step: Run 'npm run build:wasm' to build WASM decoder module"
