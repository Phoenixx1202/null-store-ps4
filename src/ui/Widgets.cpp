#include "ui/Widgets.h"

#include "ui/Theme.h"

namespace ns::ui {

using namespace theme;

namespace {

constexpr float kHintGap = 30.0f;     // between two hints
constexpr float kKeyLabelGap = 10.0f; // between a key and its label
constexpr float kKeyPairGap = 6.0f;   // between the keys of one hint

const char* keyLabel(Key key)
{
    switch (key) {
    case Key::L1: return "L1";
    case Key::R1: return "R1";
    case Key::L2: return "L2";
    case Key::R2: return "R2";
    case Key::L3: return "L3";
    case Key::R3: return "R3";
    case Key::Options: return "OPTIONS";
    default: return nullptr;
    }
}

int keyFontSize(float size)
{
    return static_cast<int>(std::lround(size * 18.0f / 38.0f));
}

float smoothstep(float edge0, float edge1, float x)
{
    const float t = clampf((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

float baseline(Renderer& r, float centerY, int size)
{
    return std::floor(centerY + r.capHeight(size) * 0.5f + 0.5f);
}

float keyWidth(Renderer& r, Key key, float size)
{
    if (const char* label = keyLabel(key)) {
        const int fontSize = keyFontSize(size);
        const float labelWidth = static_cast<float>(std::ceil(r.measureText(label, fontSize)));
        return std::max(size, labelWidth + static_cast<float>(fontSize));
    }
    return key == Key::Touchpad ? std::floor(size * 1.3f) : size;
}

float key(Renderer& r, Key which, float x, float y, float size)
{
    const float width = keyWidth(r, which, size);
    const Rect body(std::floor(x), std::floor(y), width, size);
    r.roundedRect(body, size * 0.5f, kKeyFill);
    r.roundedOutline(body, size * 0.5f, 1.0f, kKeyBorder);

    if (const char* label = keyLabel(which)) {
        const int fontSize = keyFontSize(size);
        r.textCentered(label, body.x + width * 0.5f, baseline(r, body.y + size * 0.5f, fontSize), fontSize, kText);
        return width;
    }
    ButtonGlyph glyph = ButtonGlyph::Cross;
    switch (which) {
    case Key::Circle: glyph = ButtonGlyph::Circle; break;
    case Key::Square: glyph = ButtonGlyph::Square; break;
    case Key::Triangle: glyph = ButtonGlyph::Triangle; break;
    case Key::Touchpad: glyph = ButtonGlyph::Touchpad; break;
    default: break;
    }
    r.buttonGlyph(glyph, Rect(body.x + (width - size) * 0.5f, body.y, size, size), kText);
    return width;
}

float hintsWidth(Renderer& r, const std::vector<Hint>& hints, float size)
{
    float width = 0.0f;
    for (size_t i = 0; i < hints.size(); ++i) {
        for (size_t k = 0; k < hints[i].keys.size(); ++k) {
            width += keyWidth(r, hints[i].keys[k], size) + (k ? kKeyPairGap : 0.0f);
        }
        width += kKeyLabelGap + r.measureText(hints[i].label, kFontLabel);
        if (i + 1 < hints.size()) {
            width += kHintGap;
        }
    }
    return width;
}

float hintsFrom(Renderer& r, float x, float y, const std::vector<Hint>& hints, float size)
{
    for (size_t i = 0; i < hints.size(); ++i) {
        for (size_t k = 0; k < hints[i].keys.size(); ++k) {
            if (k) {
                x += kKeyPairGap;
            }
            x += key(r, hints[i].keys[k], x, y, size);
        }
        x += kKeyLabelGap;
        x += r.text(hints[i].label, x, baseline(r, y + size * 0.5f, kFontLabel), kFontLabel, kMuted);
        if (i + 1 < hints.size()) {
            x += kHintGap;
        }
    }
    return x;
}

void hintsUntil(Renderer& r, float right, float y, const std::vector<Hint>& hints, float size)
{
    hintsFrom(r, right - hintsWidth(r, hints, size), y, hints, size);
}

void background(Renderer& r, const Color& glow, float strength)
{
    // A coarse grid with a colour per corner: three vertical stops, a pool of
    // light behind the content and a vignette, without a texture.
    constexpr int kColumns = 32;
    constexpr int kRows = 18;
    Color colors[(kColumns + 1) * (kRows + 1)];
    const Color top(0.016f, 0.046f, 0.036f);
    const Color middle(0.026f, 0.078f, 0.058f);
    const Color bottom(0.008f, 0.020f, 0.016f);
    constexpr float kHorizon = 0.75f;
    for (int y = 0; y <= kRows; ++y) {
        for (int x = 0; x <= kColumns; ++x) {
            const float u = static_cast<float>(x) / kColumns;
            const float v = static_cast<float>(y) / kRows;
            Color color = v < kHorizon ? Color::lerp(top, middle, smoothstep(0.0f, kHorizon, v))
                                       : Color::lerp(middle, bottom, smoothstep(kHorizon, 1.0f, v));
            const float dx = (u - 0.5f) * 1.77778f;
            const float dy = v - 0.46f;
            const float distance = std::sqrt(dx * dx / (0.58f * 0.58f) + dy * dy / (0.40f * 0.40f));
            const float light =
                (std::exp(-distance * distance * 2.2f) * 0.85f + std::exp(-distance * 1.6f) * 0.55f) * 0.20f * strength;
            const float vignette =
                1.0f - 0.55f * smoothstep(0.35f, 1.25f, std::sqrt(dx * dx + (v - 0.5f) * (v - 0.5f)));
            color.r = std::min(1.0f, (color.r + glow.r * light) * vignette);
            color.g = std::min(1.0f, (color.g + glow.g * light) * vignette);
            color.b = std::min(1.0f, (color.b + glow.b * light) * vignette);
            colors[y * (kColumns + 1) + x] = color;
        }
    }
    const float cellW = kScreenW / kColumns;
    const float cellH = kScreenH / kRows;
    for (int y = 0; y < kRows; ++y) {
        for (int x = 0; x < kColumns; ++x) {
            const Color* row = &colors[y * (kColumns + 1) + x];
            r.rectColors(Rect(x * cellW, y * cellH, cellW, cellH), row[0], row[1], row[kColumns + 1],
                         row[kColumns + 2]);
        }
    }
}

void surface(Renderer& r, const Rect& rect, float radius, bool focused)
{
    r.roundedRect(rect, radius, focused ? kGlassFocus : kGlass);
    if (!focused) {
        r.roundedOutline(rect, radius, 1.0f, kGlassBorder);
    }
}

void panel(Renderer& r, const Rect& rect, float radius)
{
    shadow(r, rect, radius, 1.0f);
    r.roundedRect(rect, radius, kPanel);
    r.roundedOutline(rect, radius, 1.0f, kGlassBorder);
}

void pillButton(Renderer& r, const Rect& rect, const std::string& label, int size, bool focused)
{
    r.roundedRect(rect, rect.h * 0.5f, focused ? kFocusFill : kGlass);
    if (!focused) {
        r.roundedOutline(rect, rect.h * 0.5f, 1.0f, kGlassBorder);
    }
    r.textCentered(label, rect.x + rect.w * 0.5f, baseline(r, rect.y + rect.h * 0.5f, size), size,
                   focused ? kInk : kText);
}

void shadow(Renderer& r, const Rect& rect, float radius, float strength)
{
    // Cheap soft shadow: a few translucent rounded rectangles, each larger
    // and lower than the last.
    for (int layer = 4; layer >= 1; --layer) {
        const float grow = static_cast<float>(layer) * 7.0f;
        const Rect spread(rect.x - grow, rect.y - grow + 10.0f, rect.w + 2 * grow, rect.h + 2 * grow);
        r.roundedRect(spread, radius + grow, Color(0.0f, 0.0f, 0.0f, 0.10f * strength));
    }
}

namespace {

// How bright the travelling highlight is at `position` of the perimeter when
// its head is at `head` (both in turns).
float reflectionAt(float position, float head)
{
    constexpr float kLength = 0.14f; // share of the perimeter the highlight covers on each side
    float turn = position - head + 0.5f;
    turn -= std::floor(turn);
    const float strength = clampf(1.0f - std::fabs(turn - 0.5f) / kLength, 0.0f, 1.0f);
    return strength * strength * (3.0f - 2.0f * strength);
}

} // namespace

void selectionReflection(Renderer& r, const Rect& rect, float radius, float phase, float strength)
{
    constexpr float kResting = 0.30f; // the line away from the highlight
    // A second, fainter highlight runs opposite the first.
    const auto shine = [phase](float t) {
        return std::max(reflectionAt(t, phase), 0.35f * reflectionAt(t, phase + 0.5f));
    };
    // Soft light around the highlight, then the line itself.
    r.roundedOutlineTraced(rect.inset(-3.0f), radius + 3.0f, 5.0f,
                           [&](float t) { return kText.withAlpha(0.22f * shine(t) * strength); });
    r.roundedOutlineTraced(rect.inset(-1.0f), radius + 1.0f, 2.0f, [&](float t) {
        return kText.withAlpha((kResting + (1.0f - kResting) * shine(t)) * strength);
    });
}

void glassSweep(Renderer& r, const Rect& rect, float radius, float progress, float intensity)
{
    if (rect.empty() || intensity <= 0.0f) {
        return;
    }
    // The band is slanted: its position along a row depends on the row. Drawn
    // as thin rows, each a run of gradients, kept inside the rounded corners.
    constexpr float kHalfWidth = 0.18f; // of the picture's width
    constexpr float kSlant = 0.45f;
    constexpr float kRow = 4.0f;
    const float offsets[] = { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f };
    const float opacity[] = { 0.0f, 0.3f, 1.0f, 0.3f, 0.0f };
    const float center = -1.0f + 2.0f * clampf(progress, 0.0f, 1.0f);
    const float corner = std::min(radius, std::min(rect.w, rect.h) * 0.5f);

    for (float top = rect.y; top < rect.bottom(); top += kRow) {
        const float height = std::min(kRow, rect.bottom() - top);
        const float middleY = top + height * 0.5f;
        // How far the rounded corner cuts into this row.
        float inset = 0.0f;
        const float fromEdge = std::min(middleY - rect.y, rect.bottom() - middleY);
        if (fromEdge < corner) {
            const float dy = corner - fromEdge;
            inset = corner - std::sqrt(std::max(0.0f, corner * corner - dy * dy));
        }
        const float minX = rect.x + inset;
        const float maxX = rect.right() - inset;
        const float bandX =
            rect.x + rect.w * 0.5f + rect.w * (center + kSlant * (middleY - (rect.y + rect.h * 0.5f)) / rect.h);
        const float bandHalf = rect.w * kHalfWidth;
        if (bandX + bandHalf <= minX || bandX - bandHalf >= maxX) {
            continue;
        }
        // Brightness at any x of the row, from the band's profile.
        const auto brightness = [&](float x) {
            const float position = clampf((x - bandX) / bandHalf, -1.0f, 1.0f);
            for (int i = 0; i < 4; ++i) {
                if (position <= offsets[i + 1]) {
                    const float t = (position - offsets[i]) / (offsets[i + 1] - offsets[i]);
                    return opacity[i] + (opacity[i + 1] - opacity[i]) * t;
                }
            }
            return 0.0f;
        };
        for (int i = 0; i < 4; ++i) {
            const float x0 = clampf(bandX + bandHalf * offsets[i], minX, maxX);
            const float x1 = clampf(bandX + bandHalf * offsets[i + 1], minX, maxX);
            if (x1 - x0 < 0.25f) {
                continue;
            }
            const Color left = kText.withAlpha(intensity * brightness(x0));
            const Color right = kText.withAlpha(intensity * brightness(x1));
            r.rectColors(Rect(x0, top, x1 - x0, height), left, right, left, right);
        }
    }
}

float chipWidth(Renderer& r, const std::string& label)
{
    return std::ceil(r.measureText(label, kFontLabel)) + 32.0f;
}

float chip(Renderer& r, float x, float y, const std::string& label, const Color& text, const Color& fill)
{
    const float width = chipWidth(r, label);
    const Rect rect(std::floor(x), std::floor(y), width, 38.0f);
    if (fill.a > 0.0f) {
        r.roundedRect(rect, 19.0f, fill);
    } else {
        r.roundedOutline(rect, 19.0f, 1.0f, kGlassBorder);
    }
    r.text(label, rect.x + 16.0f, baseline(r, rect.y + 19.0f, kFontLabel), kFontLabel, text);
    return width;
}

void progressBar(Renderer& r, const Rect& rect, float fraction, const Color& fill)
{
    r.roundedRect(rect, rect.h * 0.5f, Color(1.0f, 1.0f, 1.0f, 0.10f));
    const float width = rect.w * clampf(fraction, 0.0f, 1.0f);
    if (width >= rect.h) {
        r.roundedRect(Rect(rect.x, rect.y, width, rect.h), rect.h * 0.5f, fill);
    } else if (width > 0.5f) {
        r.roundedRect(Rect(rect.x, rect.y, rect.h, rect.h), rect.h * 0.5f, fill.withAlpha(fill.a * width / rect.h));
    }
}

} // namespace ns::ui
