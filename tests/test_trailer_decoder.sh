#!/bin/sh
# Optional host playback regression. Requires FFmpeg development files and CLI, libwebp and pkg-config.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
libs="libavformat libavcodec libavutil libswresample libswscale libwebp"
${CXX:-clang++} -std=c++17 -O2 ${CXXFLAGS:-} -Isrc -Ithird_party -Ithird_party/libwebp \
    $(pkg-config --cflags $libs) tests/test_trailer_decoder.cpp src/app/TrailerDecoder.cpp src/app/TrailerStream.cpp src/core/Log.cpp src/graphics/Image.cpp \
    $(pkg-config --libs $libs) -o "$tmp/check"
"$tmp/check" - timeline
"$tmp/check" - buffer
"$tmp/check" - buffer-cancel
ffmpeg -nostdin -v error -f lavfi -i testsrc2=size=320x180:rate=24 -f lavfi -i sine=sample_rate=44100 \
    -t 1 -c:v libx264 -bf 3 -c:a aac "$tmp/clip.mp4"
ffmpeg -nostdin -v error -i "$tmp/clip.mp4" -map 0:v -c copy "$tmp/silent.mp4"
"$tmp/check" "$tmp/clip.mp4" audio
"$tmp/check" "$tmp/clip.mp4" stream
"$tmp/check" "$tmp/clip.mp4" stream-cancel
ffmpeg -nostdin -v error -i "$tmp/clip.mp4" -c copy -movflags +faststart "$tmp/fast.mp4"
"$tmp/check" "$tmp/fast.mp4" stream
"$tmp/check" "$tmp/silent.mp4" silent
"$tmp/check" "$tmp/clip.mp4" cancel
printf 'invalid mp4' > "$tmp/invalid.mp4"
"$tmp/check" "$tmp/invalid.mp4" invalid
if [ "$#" -gt 0 ]; then "$tmp/check" "$1" stream; fi
