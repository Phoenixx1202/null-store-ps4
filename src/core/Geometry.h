#pragma once

#include <cstdlib> // must precede <cmath> with the toolchain's libc++ (::abs)
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ns {

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    constexpr Rect() = default;
    constexpr Rect(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}

    float right() const { return x + w; }
    float bottom() const { return y + h; }
    bool empty() const { return w <= 0.0f || h <= 0.0f; }

    Rect inset(float d) const { return Rect(x + d, y + d, w - 2 * d, h - 2 * d); }

    bool intersects(const Rect& o) const
    {
        return !(o.x >= right() || o.right() <= x || o.y >= bottom() || o.bottom() <= y);
    }

    Rect intersection(const Rect& o) const
    {
        const float nx = std::max(x, o.x);
        const float ny = std::max(y, o.y);
        const float nr = std::min(right(), o.right());
        const float nb = std::min(bottom(), o.bottom());
        if (nr <= nx || nb <= ny) {
            return Rect();
        }
        return Rect(nx, ny, nr - nx, nb - ny);
    }
};

// Straight (non-premultiplied) colour, components in 0..1.
struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    constexpr Color() = default;
    constexpr Color(float r_, float g_, float b_, float a_ = 1.0f) : r(r_), g(g_), b(b_), a(a_) {}

    // 0xRRGGBB, the notation used throughout the layout code.
    static constexpr Color hex(uint32_t rgb, float alpha = 1.0f)
    {
        return Color(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, alpha);
    }

    constexpr Color withAlpha(float alpha) const { return Color(r, g, b, alpha); }

    static Color lerp(const Color& from, const Color& to, float t)
    {
        return Color(from.r + (to.r - from.r) * t, from.g + (to.g - from.g) * t, from.b + (to.b - from.b) * t,
                     from.a + (to.a - from.a) * t);
    }
};

inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

} // namespace ns
