#include "graphics/Image.h"

#include <cstdio>
#include <cstring>

// The single stb_image implementation of the program. The catalogue refresh
// (src/app/CatalogRefresh.cpp) uses stbi_info_from_memory from here as well.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#define STBI_MAX_DIMENSIONS 4096
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_FAILURE_STRINGS
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#pragma clang diagnostic pop

// WebP through libwebp's decoder (third_party/libwebp).
#include "src/webp/decode.h"

namespace ns {

namespace {

// Anti-aliased coverage of the rounded rectangle [0,w]x[0,h] at (px, py), from
// its exact signed distance.
float roundedRectCoverage(float px, float py, float w, float h, float radius)
{
    radius = std::min(radius, std::min(w, h) * 0.5f);
    const float halfW = w * 0.5f;
    const float halfH = h * 0.5f;
    const float qx = std::fabs(px - halfW) - (halfW - radius);
    const float qy = std::fabs(py - halfH) - (halfH - radius);
    const float ox = std::max(qx, 0.0f);
    const float oy = std::max(qy, 0.0f);
    const float distance = std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - radius;
    return clampf(0.5f - distance, 0.0f, 1.0f);
}

float segmentDistance(float px, float py, float ax, float ay, float bx, float by)
{
    const float abx = bx - ax;
    const float aby = by - ay;
    const float apx = px - ax;
    const float apy = py - ay;
    const float lengthSq = abx * abx + aby * aby;
    const float t = lengthSq > 0.0f ? clampf((apx * abx + apy * aby) / lengthSq, 0.0f, 1.0f) : 0.0f;
    const float dx = apx - abx * t;
    const float dy = apy - aby * t;
    return std::sqrt(dx * dx + dy * dy);
}

// Premultiplied white with the given coverage.
void setCoverage(uint8_t* pixel, float coverage)
{
    const uint8_t value = static_cast<uint8_t>(clampf(coverage, 0.0f, 1.0f) * 255.0f + 0.5f);
    pixel[0] = pixel[1] = pixel[2] = pixel[3] = value;
}

} // namespace

bool readBinaryFile(const std::string& path, std::vector<uint8_t>& out, size_t limit)
{
    out.clear();
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) {
        return false;
    }
    uint8_t buffer[16384];
    size_t count = 0;
    bool ok = true;
    while ((count = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (out.size() + count > limit) {
            ok = false;
            break;
        }
        out.insert(out.end(), buffer, buffer + count);
    }
    fclose(file);
    if (!ok || out.empty()) {
        out.clear();
        return false;
    }
    return true;
}

void Image::reset(int width, int height)
{
    width_ = std::max(0, width);
    height_ = std::max(0, height);
    pixels_.assign(static_cast<size_t>(width_) * height_ * 4, 0);
}

bool Image::decode(const uint8_t* encoded, size_t length, int maxDimension)
{
    int w = 0;
    int h = 0;
    const bool webp = length > 12 && memcmp(encoded, "RIFF", 4) == 0 && memcmp(encoded + 8, "WEBP", 4) == 0;
    if (webp) {
        if (!WebPGetInfo(encoded, length, &w, &h) || w > STBI_MAX_DIMENSIONS || h > STBI_MAX_DIMENSIONS) {
            return false;
        }
        uint8_t* decoded = WebPDecodeRGBA(encoded, length, &w, &h);
        if (!decoded) {
            return false;
        }
        reset(w, h);
        memcpy(pixels_.data(), decoded, pixels_.size());
        WebPFree(decoded);
    } else {
        int channels = 0;
        stbi_uc* decoded = stbi_load_from_memory(encoded, static_cast<int>(length), &w, &h, &channels, 4);
        if (!decoded) {
            return false;
        }
        reset(w, h);
        memcpy(pixels_.data(), decoded, pixels_.size());
        stbi_image_free(decoded);
    }
    premultiply();

    if (maxDimension > 0 && (w > maxDimension || h > maxDimension)) {
        const float scale = static_cast<float>(maxDimension) / static_cast<float>(std::max(w, h));
        shrinkTo(std::max(1, static_cast<int>(w * scale + 0.5f)), std::max(1, static_cast<int>(h * scale + 0.5f)));
    }
    return true;
}

bool Image::load(const std::string& path, int maxDimension)
{
    std::vector<uint8_t> bytes;
    return readBinaryFile(path, bytes) && decode(bytes.data(), bytes.size(), maxDimension);
}

void Image::premultiply()
{
    for (size_t i = 0; i < pixels_.size(); i += 4) {
        const uint32_t a = pixels_[i + 3];
        if (a == 255) {
            continue;
        }
        pixels_[i] = static_cast<uint8_t>((pixels_[i] * a + 127) / 255);
        pixels_[i + 1] = static_cast<uint8_t>((pixels_[i + 1] * a + 127) / 255);
        pixels_[i + 2] = static_cast<uint8_t>((pixels_[i + 2] * a + 127) / 255);
    }
}

void Image::shrinkTo(int width, int height)
{
    if (empty() || width <= 0 || height <= 0 || (width == width_ && height == height_)) {
        return;
    }
    // Each destination pixel averages the block of source pixels it covers.
    std::vector<uint8_t> out(static_cast<size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        const int y0 = static_cast<int>(static_cast<int64_t>(y) * height_ / height);
        const int y1 = std::max(y0 + 1, static_cast<int>(static_cast<int64_t>(y + 1) * height_ / height));
        for (int x = 0; x < width; ++x) {
            const int x0 = static_cast<int>(static_cast<int64_t>(x) * width_ / width);
            const int x1 = std::max(x0 + 1, static_cast<int>(static_cast<int64_t>(x + 1) * width_ / width));
            uint32_t sum[4] = { 0, 0, 0, 0 };
            uint32_t count = 0;
            for (int sy = y0; sy < y1 && sy < height_; ++sy) {
                const uint8_t* row = &pixels_[(static_cast<size_t>(sy) * width_ + x0) * 4];
                for (int sx = x0; sx < x1 && sx < width_; ++sx, row += 4) {
                    sum[0] += row[0];
                    sum[1] += row[1];
                    sum[2] += row[2];
                    sum[3] += row[3];
                    ++count;
                }
            }
            uint8_t* dst = &out[(static_cast<size_t>(y) * width + x) * 4];
            for (int c = 0; c < 4; ++c) {
                dst[c] = static_cast<uint8_t>(count ? (sum[c] + count / 2) / count : 0);
            }
        }
    }
    pixels_.swap(out);
    width_ = width;
    height_ = height;
}

Image Image::resampled(float sx, float sy, float sw, float sh, int width, int height) const
{
    Image out(width, height);
    if (empty() || width <= 0 || height <= 0 || sw <= 0.0f || sh <= 0.0f) {
        return out;
    }
    const float stepX = sw / static_cast<float>(width);
    const float stepY = sh / static_cast<float>(height);
    const bool shrinking = stepX > 1.0f || stepY > 1.0f;
    const auto pixel = [this](int x, int y) {
        x = std::max(0, std::min(width_ - 1, x));
        y = std::max(0, std::min(height_ - 1, y));
        return &pixels_[(static_cast<size_t>(y) * width_ + x) * 4];
    };
    for (int y = 0; y < height; ++y) {
        const float top = sy + y * stepY;
        for (int x = 0; x < width; ++x) {
            const float left = sx + x * stepX;
            uint8_t* dst = out.data() + (static_cast<size_t>(y) * width + x) * 4;
            if (shrinking) {
                // Average the block of source pixels this pixel covers.
                const int x0 = static_cast<int>(std::floor(left));
                const int y0 = static_cast<int>(std::floor(top));
                const int x1 = std::max(x0 + 1, static_cast<int>(std::ceil(left + stepX)));
                const int y1 = std::max(y0 + 1, static_cast<int>(std::ceil(top + stepY)));
                uint32_t sum[4] = { 0, 0, 0, 0 };
                for (int yy = y0; yy < y1; ++yy) {
                    for (int xx = x0; xx < x1; ++xx) {
                        const uint8_t* p = pixel(xx, yy);
                        sum[0] += p[0];
                        sum[1] += p[1];
                        sum[2] += p[2];
                        sum[3] += p[3];
                    }
                }
                const uint32_t count = static_cast<uint32_t>((x1 - x0) * (y1 - y0));
                for (int c = 0; c < 4; ++c) {
                    dst[c] = static_cast<uint8_t>((sum[c] + count / 2) / count);
                }
            } else {
                const float fx = left + stepX * 0.5f - 0.5f;
                const float fy = top + stepY * 0.5f - 0.5f;
                const int x0 = static_cast<int>(std::floor(fx));
                const int y0 = static_cast<int>(std::floor(fy));
                const float tx = fx - x0;
                const float ty = fy - y0;
                const uint8_t* p00 = pixel(x0, y0);
                const uint8_t* p10 = pixel(x0 + 1, y0);
                const uint8_t* p01 = pixel(x0, y0 + 1);
                const uint8_t* p11 = pixel(x0 + 1, y0 + 1);
                for (int c = 0; c < 4; ++c) {
                    const float upper = p00[c] * (1 - tx) + p10[c] * tx;
                    const float lower = p01[c] * (1 - tx) + p11[c] * tx;
                    dst[c] = static_cast<uint8_t>(upper * (1 - ty) + lower * ty + 0.5f);
                }
            }
        }
    }
    return out;
}

void Image::roundCorners(float radius)
{
    if (radius <= 0.0f || empty()) {
        return;
    }
    const int reach = static_cast<int>(std::ceil(radius)) + 1;
    for (int y = 0; y < height_; ++y) {
        if (y >= reach && y < height_ - reach) {
            continue;
        }
        for (int x = 0; x < width_; ++x) {
            if (x >= reach && x < width_ - reach) {
                continue;
            }
            const float coverage = roundedRectCoverage(x + 0.5f, y + 0.5f, static_cast<float>(width_),
                                                       static_cast<float>(height_), radius);
            if (coverage >= 1.0f) {
                continue;
            }
            uint8_t* p = &pixels_[(static_cast<size_t>(y) * width_ + x) * 4];
            for (int c = 0; c < 4; ++c) { // premultiplied: every channel scales
                p[c] = static_cast<uint8_t>(p[c] * coverage + 0.5f);
            }
        }
    }
}

Image Image::solidWhite(int size)
{
    Image image(size, size);
    memset(image.data(), 255, image.bytes());
    return image;
}

Image Image::roundedSquare(float radius)
{
    const int r = std::max(1, static_cast<int>(std::ceil(radius)));
    const int size = r * 2 + 2; // a straight run in the middle for the 9-slice centre
    Image image(size, size);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            setCoverage(image.data() + (static_cast<size_t>(y) * size + x) * 4,
                        roundedRectCoverage(x + 0.5f, y + 0.5f, static_cast<float>(size), static_cast<float>(size),
                                            radius));
        }
    }
    return image;
}

Image Image::roundedOutline(float radius, float thickness)
{
    const int r = std::max(1, static_cast<int>(std::ceil(radius)));
    const int size = r * 2 + 2;
    Image image(size, size);
    const float innerRadius = std::max(0.0f, radius - thickness);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float outer = roundedRectCoverage(x + 0.5f, y + 0.5f, static_cast<float>(size),
                                                    static_cast<float>(size), radius);
            const float inner = roundedRectCoverage(x + 0.5f - thickness, y + 0.5f - thickness,
                                                    static_cast<float>(size) - 2 * thickness,
                                                    static_cast<float>(size) - 2 * thickness, innerRadius);
            setCoverage(image.data() + (static_cast<size_t>(y) * size + x) * 4, outer - inner);
        }
    }
    return image;
}

Image Image::buttonGlyph(ButtonGlyph glyph, int size)
{
    Image image(size, size);
    const float s = static_cast<float>(size);
    const float halfStroke = std::max(1.3f, s * 0.05f) * 0.5f; // a fine line, like the console's own hints
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float px = x + 0.5f;
            const float py = y + 0.5f;
            float d = 1e9f;
            float holes = -1e9f; // filled dots (the touch pad's speaker)
            switch (glyph) {
            case ButtonGlyph::Cross:
                d = std::min(segmentDistance(px, py, s * 0.31f, s * 0.31f, s * 0.69f, s * 0.69f),
                             segmentDistance(px, py, s * 0.69f, s * 0.31f, s * 0.31f, s * 0.69f));
                break;
            case ButtonGlyph::Circle: {
                const float dx = px - s * 0.5f;
                const float dy = py - s * 0.5f;
                d = std::fabs(std::sqrt(dx * dx + dy * dy) - s * 0.24f);
                break;
            }
            case ButtonGlyph::Triangle:
                d = std::min({ segmentDistance(px, py, s * 0.5f, s * 0.27f, s * 0.26f, s * 0.70f),
                               segmentDistance(px, py, s * 0.26f, s * 0.70f, s * 0.74f, s * 0.70f),
                               segmentDistance(px, py, s * 0.74f, s * 0.70f, s * 0.5f, s * 0.27f) });
                break;
            case ButtonGlyph::Square:
                d = std::min({ segmentDistance(px, py, s * 0.28f, s * 0.28f, s * 0.72f, s * 0.28f),
                               segmentDistance(px, py, s * 0.72f, s * 0.28f, s * 0.72f, s * 0.72f),
                               segmentDistance(px, py, s * 0.72f, s * 0.72f, s * 0.28f, s * 0.72f),
                               segmentDistance(px, py, s * 0.28f, s * 0.72f, s * 0.28f, s * 0.28f) });
                break;
            case ButtonGlyph::Star:
                // Five-pointed outline: ten segments between outer and inner points.
                for (int i = 0; i < 10; ++i) {
                    const float a0 = -1.5707963f + i * 0.6283185f;
                    const float a1 = a0 + 0.6283185f;
                    const float r0 = (i & 1) ? s * 0.17f : s * 0.40f;
                    const float r1 = (i & 1) ? s * 0.40f : s * 0.17f;
                    d = std::min(d, segmentDistance(px, py, s * 0.5f + std::cos(a0) * r0, s * 0.52f + std::sin(a0) * r0,
                                                    s * 0.5f + std::cos(a1) * r1, s * 0.52f + std::sin(a1) * r1));
                }
                break;
            case ButtonGlyph::Touchpad: {
                // The DS4's pad seen from above: straight along the top, its
                // sides leaning in, the lower edge curved...
                const float outline[][2] = { { 0.17f, 0.27f }, { 0.83f, 0.27f }, { 0.79f, 0.52f }, { 0.70f, 0.585f },
                                             { 0.50f, 0.605f }, { 0.30f, 0.585f }, { 0.21f, 0.52f }, { 0.17f, 0.27f } };
                for (int i = 0; i + 1 < 8; ++i) {
                    d = std::min(d, segmentDistance(px, py, s * outline[i][0], s * outline[i][1], s * outline[i + 1][0],
                                                    s * outline[i + 1][1]));
                }
                // ...and under it the speaker's holes: two staggered rows of dots.
                const float dot = std::max(0.9f, s * 0.026f);
                for (int i = 0; i < 9; ++i) {
                    const int row = i < 5 ? 0 : 1;
                    const float cx = s * (row == 0 ? 0.38f + i * 0.06f : 0.41f + (i - 5) * 0.06f);
                    const float cy = s * (row == 0 ? 0.70f : 0.765f);
                    const float dx = px - cx;
                    const float dy = py - cy;
                    holes = std::max(holes, dot + 0.5f - static_cast<float>(std::sqrt(dx * dx + dy * dy)));
                }
                break;
            }
            }
            setCoverage(image.data() + (static_cast<size_t>(y) * size + x) * 4,
                        std::max(halfStroke + 0.5f - d, holes));
        }
    }
    return image;
}

} // namespace ns
