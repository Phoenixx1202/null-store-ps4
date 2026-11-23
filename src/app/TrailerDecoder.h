#pragma once

#include "graphics/Image.h"

#include <cstdint>
#include <functional>
#include <string>

namespace ns {

class TrailerStream;

struct TrailerOutput {
    int maxVideoWidth = 960;
    std::function<bool()> cancelled;
    // Time from the first video frame, in microseconds.
    std::function<void(Image&, int64_t)> video;
    // Interleaved signed 16-bit stereo at 48 kHz.
    std::function<void(const int16_t*, int)> audio;
};

bool decodeTrailer(TrailerStream& stream, const TrailerOutput& output, std::string& error);
bool decodeTrailer(const std::string& file, const TrailerOutput& output, std::string& error);

} // namespace ns
