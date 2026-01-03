#!/bin/bash
# FFmpeg WASM 编译脚本 - M1 阶段 (仅 H.264 解码器)
set -e

# ===========================
# 配置参数
# ===========================
FFMPEG_VERSION="7.1.2"
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
THIRD_PARTY_DIR="${PROJECT_ROOT}/third_party"
FFMPEG_SRC_DIR="${THIRD_PARTY_DIR}/ffmpeg-${FFMPEG_VERSION}"
INSTALL_DIR="${PROJECT_ROOT}/ffmpeg-build"

# Emscripten 配置
export EMSDK="/home/zhiwei/OpenSource/emsdk"
source "${EMSDK}/emsdk_env.sh"

# ===========================
# 检查环境
# ===========================
echo "🔍 检查编译环境..."
if [ ! -d "${FFMPEG_SRC_DIR}" ]; then
    echo "❌ 错误: FFmpeg 源码不存在，请先运行: bash build/download_ffmpeg.sh"
    exit 1
fi

which emcc > /dev/null || {
    echo "❌ 错误: 未找到 emcc"
    exit 1
}

echo "✅ Emscripten: $(emcc --version | head -1)"
echo "✅ FFmpeg 源码: ${FFMPEG_SRC_DIR}"
echo "✅ 安装目标: ${INSTALL_DIR}"

# ===========================
# 清理旧构建
# ===========================
echo ""
echo "🧹 清理旧构建..."
rm -rf "${INSTALL_DIR}"
mkdir -p "${INSTALL_DIR}"

cd "${FFMPEG_SRC_DIR}"
make distclean 2>/dev/null || true

# ===========================
# FFmpeg 配置
# ===========================
echo ""
echo "⚙️  配置 FFmpeg (M1: 最小化 H.264 解码器)..."

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
    --enable-parser=h264 \
    --enable-protocol=file \
    \
    --extra-cflags="${CFLAGS}" \
    --extra-ldflags="${LDFLAGS}"

echo "✅ FFmpeg 配置完成"

# ===========================
# 编译 (预计 10-20 分钟)
# ===========================
echo ""
echo "🔨 编译 FFmpeg (这可能需要 10-20 分钟)..."
echo "   开始时间: $(date '+%H:%M:%S')"

NPROC=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
emmake make -j${NPROC}

echo "   结束时间: $(date '+%H:%M:%S')"
echo "✅ FFmpeg 编译完成"

# ===========================
# 安装
# ===========================
echo ""
echo "📦 安装 FFmpeg 库..."
emmake make install
echo "✅ FFmpeg 安装完成: ${INSTALL_DIR}"

# ===========================
# 验证
# ===========================
echo ""
echo "🔍 验证编译产物..."
REQUIRED_LIBS=("lib/libavcodec.a" "lib/libavformat.a" "lib/libavutil.a")
ALL_OK=true

for lib in "${REQUIRED_LIBS[@]}"; do
    if [ -f "${INSTALL_DIR}/${lib}" ]; then
        SIZE=$(du -h "${INSTALL_DIR}/${lib}" | cut -f1)
        echo "  ✅ ${lib} (${SIZE})"
    else
        echo "  ❌ 缺失: ${lib}"
        ALL_OK=false
    fi
done

if [ "$ALL_OK" = false ]; then
    echo ""
    echo "❌ 编译验证失败，部分库文件缺失"
    exit 1
fi

echo ""
echo "🎉 FFmpeg 编译成功!"
echo "   版本: ${FFMPEG_VERSION}"
echo "   安装目录: ${INSTALL_DIR}"
echo ""
echo "下一步: 运行 'npm run build:wasm' 编译 WASM 解码模块"
