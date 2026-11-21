#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/Geometry.h"

// -----------------------------------------------------------------------------
// CPU-side RGBA8 bitmap. Everything handed to the renderer is stored with
// PREMULTIPLIED alpha (the GPU blends with ONE / ONE_MINUS_SRC_ALPHA).
// -----------------------------------------------------------------------------
namespace ns {

// DualShock face-button symbols, drawn procedurally (they are not in the font).
enum class ButtonGlyph { Cross = 0, Circle = 1, Triangle = 2, Square = 3, Star = 4, Touchpad = 5 };

class Image {
public:
    Image() = default;
    Image(int width, int height) { reset(width, height); }

    int width() const { return width_; }
    int height() const { return height_; }
    bool empty() const { return width_ <= 0 || height_ <= 0; }
    const uint8_t* data() const { return pixels_.data(); }
    uint8_t* data() { return pixels_.data(); }
    size_t bytes() const { return pixels_.size(); }

    void reset(int width, int height);

    // Decodes PNG, JPEG or WebP. When maxDimension > 0 the picture is reduced so that
    // max(width, height) <= maxDimension. The result is premultiplied.
    bool decode(const uint8_t* encoded, size_t length, int maxDimension = 0);
    bool load(const std::string& path, int maxDimension = 0);

    void premultiply();
    // Box-filter reduction; only ever used to shrink.
    void shrinkTo(int width, int height);
    // The part (sx, sy, sw, sh) of this image scaled to width x height:
    // averaged when it shrinks, interpolated when it grows.
    Image resampled(float sx, float sy, float sw, float sh, int width, int height) const;
    // Cuts anti-aliased rounded corners into the picture.
    void roundCorners(float radius);

    // White masks, tinted per vertex when drawn ---------------------------------
    static Image solidWhite(int size);
    // Rounded square for 9-slice panels: side = 2 * ceil(radius) + 2.
    static Image roundedSquare(float radius);
    static Image roundedOutline(float radius, float thickness);
    static Image buttonGlyph(ButtonGlyph glyph, int size);

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<uint8_t> pixels_;
};

// Reads a whole file; false when it is missing, empty or larger than `limit`.
bool readBinaryFile(const std::string& path, std::vector<uint8_t>& out, size_t limit = 64u * 1024 * 1024);

} // namespace ns
