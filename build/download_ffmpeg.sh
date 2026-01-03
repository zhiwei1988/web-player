#!/bin/bash
set -e

FFMPEG_VERSION="7.1.2"
FFMPEG_TAG="n${FFMPEG_VERSION}"
FFMPEG_REPO="https://github.com/FFmpeg/FFmpeg.git"
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
THIRD_PARTY_DIR="${PROJECT_ROOT}/third_party"
FFMPEG_DIR="${THIRD_PARTY_DIR}/ffmpeg-${FFMPEG_VERSION}"

mkdir -p "${THIRD_PARTY_DIR}"
cd "${THIRD_PARTY_DIR}"

if [ ! -d "${FFMPEG_DIR}" ]; then
    echo "Cloning FFmpeg source code (tag: ${FFMPEG_TAG})..."
    git clone --depth 1 --branch "${FFMPEG_TAG}" "${FFMPEG_REPO}" "${FFMPEG_DIR}"
    echo "Cloning completed"
else
    echo "FFmpeg source code already exists, skipping clone"
fi

echo ""
echo "FFmpeg source code ready"
echo "   Version: ${FFMPEG_TAG}"
echo "   Source: ${FFMPEG_REPO}"
echo "   Path: ${FFMPEG_DIR}"
