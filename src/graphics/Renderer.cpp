#include "graphics/Renderer.h"

#include "core/Log.h"

namespace ns {

namespace {

constexpr uint64_t kMaskTtlFrames = 1800; // ~30 s at 60 fps
const Rect kFullUv(0.0f, 0.0f, 1.0f, 1.0f);
const Rect kWhiteUv(0.25f, 0.25f, 0.5f, 0.5f); // centre of the small white texture

// Mask cache keys: kind in the top byte, parameters below.
constexpr uint64_t kMaskRounded = 1ull << 56;
constexpr uint64_t kMaskOutline = 2ull << 56;
constexpr uint64_t kMaskGlyph = 3ull << 56;

uint64_t quarterSteps(float value)
{
    return static_cast<uint64_t>(std::lround(std::max(0.0f, value) * 4.0f)) & 0xFFFF;
}

Image paintMask(uint64_t key)
{
    const uint64_t kind = key & (0xFFull << 56);
    const float first = static_cast<float>(key & 0xFFFF) / 4.0f;
    const float second = static_cast<float>((key >> 16) & 0xFFFF) / 4.0f;
    if (kind == kMaskRounded) {
        return Image::roundedSquare(first);
    }
    if (kind == kMaskOutline) {
        return Image::roundedOutline(first, second);
    }
    return Image::buttonGlyph(static_cast<ButtonGlyph>((key >> 16) & 0xFF), static_cast<int>(key & 0xFFFF));
}

inline uint8_t toByte(float v)
{
    return static_cast<uint8_t>(clampf(v, 0.0f, 1.0f) * 255.0f + 0.5f);
}

} // namespace

Renderer::Renderer() = default;

Renderer::~Renderer()
{
    shutdown();
}

bool Renderer::init(std::unique_ptr<RenderBackend> backend, int width, int height, const std::string& fontPath)
{
    backend_ = std::move(backend);
    width_ = width;
    height_ = height;
    if (!backend_ || !backend_->init(width, height)) {
        NS_LOG_ERROR("Render backend could not start");
        return false;
    }
    if (!white_.create(*backend_, Image::solidWhite(4), false)) {
        NS_LOG_ERROR("White texture could not be created");
        return false;
    }
    if (!font_.load(*backend_, fontPath)) {
        return false;
    }
    // font.ttf -> font-fallback.ttf, beside it (optional).
    const size_t dot = fontPath.rfind('.');
    if (dot != std::string::npos && !font_.loadFallback(fontPath.substr(0, dot) + "-fallback" + fontPath.substr(dot))) {
        NS_LOG_WARN("No fallback font beside %s", fontPath.c_str());
    }
    vertices_.reserve(RenderBackend::kMaxQuads * 4);
    commands_.reserve(256);
    ready_ = true;
    NS_LOG_INFO("Renderer initialized (%s, %dx%d)", backend_->name(), width_, height_);
    return true;
}

void Renderer::shutdown()
{
    if (!backend_) {
        return;
    }
    masks_.clear();
    white_.destroy();
    font_.releaseTextures();
    backend_->shutdown();
    backend_.reset();
    ready_ = false;
}

void Renderer::beginFrame(const Color& clearColor)
{
    ++frame_;
    vertices_.clear();
    commands_.clear();
    clipStack_.clear();
    opacityStack_.clear();
    clip_ = ClipRect();
    backend_->beginFrame(clearColor);
}

void Renderer::flush()
{
    font_.uploadPending();
    if (!vertices_.empty()) {
        backend_->draw(vertices_.data(), vertices_.size(), commands_.data(), commands_.size());
    }
    vertices_.clear();
    commands_.clear();
}

bool Renderer::endFrame()
{
    flush();
    const bool shown = backend_->present();
    // Drop the masks nothing has drawn for a while.
    if (frame_ % 300 == 0) {
        for (auto it = masks_.begin(); it != masks_.end();) {
            if (frame_ - it->second.lastUsedFrame > kMaskTtlFrames) {
                it = masks_.erase(it);
            } else {
                ++it;
            }
        }
    }
    return shown;
}

void Renderer::updateClip()
{
    ClipRect clip;
    if (!clipStack_.empty()) {
        const Rect& r = clipStack_.back();
        clip.enabled = true;
        clip.x = static_cast<int>(std::floor(r.x));
        clip.y = static_cast<int>(std::floor(r.y));
        clip.w = std::max(0, static_cast<int>(std::ceil(r.right())) - clip.x);
        clip.h = std::max(0, static_cast<int>(std::ceil(r.bottom())) - clip.y);
    }
    clip_ = clip;
}

void Renderer::pushClip(const Rect& rect)
{
    const Rect screen(0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
    Rect clip = rect.intersection(screen);
    if (!clipStack_.empty()) {
        clip = clipStack_.back().intersection(clip);
    }
    clipStack_.push_back(clip);
    updateClip();
}

void Renderer::popClip()
{
    if (!clipStack_.empty()) {
        clipStack_.pop_back();
    }
    updateClip();
}

void Renderer::pushOpacity(float value)
{
    opacityStack_.push_back(opacity() * clampf(value, 0.0f, 1.0f));
}

void Renderer::popOpacity()
{
    if (!opacityStack_.empty()) {
        opacityStack_.pop_back();
    }
}

void Renderer::emitQuad(TextureId texture, const Rect& dst, const Rect& uv, const Color& c0, const Color& c1,
                        const Color& c2, const Color& c3)
{
    if (!ready_ || dst.empty() || texture == 0) {
        return;
    }
    // Skip what is off-screen or fully outside the clip rectangle.
    if (dst.right() < 0.0f || dst.bottom() < 0.0f || dst.x > static_cast<float>(width_) ||
        dst.y > static_cast<float>(height_)) {
        return;
    }
    if (!clipStack_.empty() && (clipStack_.back().empty() || !clipStack_.back().intersects(dst))) {
        return;
    }
    const float groupOpacity = opacity();
    if (groupOpacity <= 0.001f) {
        return;
    }

    if (vertices_.size() >= RenderBackend::kMaxQuads * 4) {
        flush();
    }
    const uint32_t quadIndex = static_cast<uint32_t>(vertices_.size() / 4);
    if (commands_.empty() || commands_.back().texture != texture || commands_.back().clip != clip_) {
        DrawCommand command;
        command.texture = texture;
        command.firstQuad = quadIndex;
        command.clip = clip_;
        commands_.push_back(command);
    }
    commands_.back().quadCount += 1;

    const auto push = [this, groupOpacity](float x, float y, float u, float v, const Color& c) {
        const float a = clampf(c.a * groupOpacity, 0.0f, 1.0f);
        Vertex vertex;
        vertex.x = x;
        vertex.y = y;
        vertex.u = u;
        vertex.v = v;
        vertex.r = toByte(c.r * a); // premultiplied tint
        vertex.g = toByte(c.g * a);
        vertex.b = toByte(c.b * a);
        vertex.a = toByte(a);
        vertices_.push_back(vertex);
    };
    push(dst.x, dst.y, uv.x, uv.y, c0);
    push(dst.right(), dst.y, uv.right(), uv.y, c1);
    push(dst.x, dst.bottom(), uv.x, uv.bottom(), c2);
    push(dst.right(), dst.bottom(), uv.right(), uv.bottom(), c3);
}

void Renderer::rect(const Rect& dst, const Color& color)
{
    emitQuad(white_.id(), dst, kWhiteUv, color, color, color, color);
}

void Renderer::gradientVertical(const Rect& dst, const Color& top, const Color& bottom)
{
    emitQuad(white_.id(), dst, kWhiteUv, top, top, bottom, bottom);
}

void Renderer::gradientHorizontal(const Rect& dst, const Color& left, const Color& right)
{
    emitQuad(white_.id(), dst, kWhiteUv, left, right, left, right);
}

void Renderer::rectColors(const Rect& dst, const Color& topLeft, const Color& topRight, const Color& bottomLeft,
                          const Color& bottomRight)
{
    emitQuad(white_.id(), dst, kWhiteUv, topLeft, topRight, bottomLeft, bottomRight);
}

const Texture& Renderer::mask(uint64_t key, Image (*paint)(uint64_t key))
{
    auto it = masks_.find(key);
    if (it == masks_.end()) {
        CachedMask entry;
        entry.texture.create(*backend_, paint(key), true);
        it = masks_.emplace(key, std::move(entry)).first;
    }
    it->second.lastUsedFrame = frame_;
    return it->second.texture;
}

void Renderer::nineSlice(const Texture& texture, const Rect& dst, float border, const Color& tint)
{
    if (!texture.valid()) {
        return;
    }
    const float b = std::min(border, std::min(dst.w, dst.h) * 0.5f);
    const float ub = b / static_cast<float>(texture.width());
    const float vb = b / static_cast<float>(texture.height());
    const float xs[4] = { dst.x, dst.x + b, dst.right() - b, dst.right() };
    const float ys[4] = { dst.y, dst.y + b, dst.bottom() - b, dst.bottom() };
    const float us[4] = { 0.0f, ub, 1.0f - ub, 1.0f };
    const float vs[4] = { 0.0f, vb, 1.0f - vb, 1.0f };
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            const Rect piece(xs[column], ys[row], xs[column + 1] - xs[column], ys[row + 1] - ys[row]);
            if (piece.empty()) {
                continue;
            }
            const Rect uv(us[column], vs[row], us[column + 1] - us[column], vs[row + 1] - vs[row]);
            emitQuad(texture.id(), piece, uv, tint, tint, tint, tint);
        }
    }
}

void Renderer::roundedRect(const Rect& dst, float radius, const Color& color)
{
    if (dst.empty()) {
        return;
    }
    const float r = std::min(radius, std::min(dst.w, dst.h) * 0.5f);
    if (r <= 0.5f) {
        rect(dst, color);
        return;
    }
    nineSlice(mask(kMaskRounded | quarterSteps(r), paintMask), dst, std::ceil(r), color);
}

void Renderer::roundedOutline(const Rect& dst, float radius, float thickness, const Color& color)
{
    if (dst.empty()) {
        return;
    }
    const float r = std::max(1.0f, std::min(radius, std::min(dst.w, dst.h) * 0.5f));
    nineSlice(mask(kMaskOutline | quarterSteps(r) | (quarterSteps(thickness) << 16), paintMask), dst,
              std::ceil(std::max(r, thickness)), color);
}

void Renderer::roundedOutlineTraced(const Rect& dst, float radius, float thickness,
                                    const std::function<Color(float)>& colorAt)
{
    if (dst.empty()) {
        return;
    }
    const float r = std::max(1.0f, std::min(radius, std::min(dst.w, dst.h) * 0.5f));
    const Texture& texture = mask(kMaskOutline | quarterSteps(r) | (quarterSteps(thickness) << 16), paintMask);
    if (!texture.valid()) {
        return;
    }
    // The mask is the outline of a small square: its corners draw the arcs and
    // its edges are stretched along the sides, cut into short pieces so the
    // colour can change along them.
    const float b = std::min(static_cast<float>(std::ceil(std::max(r, thickness))), std::min(dst.w, dst.h) * 0.5f);
    const float ub = b / static_cast<float>(texture.width());
    const float vb = b / static_cast<float>(texture.height());
    const float sideW = dst.w - 2.0f * b;
    const float sideH = dst.h - 2.0f * b;
    const float arc = b * 1.5707963f;
    const float perimeter = 2.0f * (sideW + sideH) + 4.0f * arc;
    constexpr float kPiece = 18.0f;
    const TextureId id = texture.id();
    const auto at = [&](float distance) { return colorAt(distance / perimeter); };

    // Sides: `forward` is the way the trace runs along the piece.
    const auto horizontal = [&](float y, float v, float start, bool forward) {
        const int pieces = std::max(1, static_cast<int>(std::ceil(sideW / kPiece)));
        for (int i = 0; i < pieces; ++i) {
            const float x0 = sideW * static_cast<float>(i) / pieces;
            const float x1 = sideW * static_cast<float>(i + 1) / pieces;
            const Color left = at(start + (forward ? x0 : sideW - x0));
            const Color right = at(start + (forward ? x1 : sideW - x1));
            emitQuad(id, Rect(dst.x + b + x0, y, x1 - x0, b), Rect(ub, v, 1.0f - 2.0f * ub, vb), left, right, left,
                     right);
        }
    };
    const auto vertical = [&](float x, float u, float start, bool forward) {
        const int pieces = std::max(1, static_cast<int>(std::ceil(sideH / kPiece)));
        for (int i = 0; i < pieces; ++i) {
            const float y0 = sideH * static_cast<float>(i) / pieces;
            const float y1 = sideH * static_cast<float>(i + 1) / pieces;
            const Color top = at(start + (forward ? y0 : sideH - y0));
            const Color bottom = at(start + (forward ? y1 : sideH - y1));
            emitQuad(id, Rect(x, dst.y + b + y0, b, y1 - y0), Rect(u, vb, ub, 1.0f - 2.0f * vb), top, top, bottom,
                     bottom);
        }
    };

    float travelled = 0.0f;
    if (sideW > 0.0f) horizontal(dst.y, 0.0f, travelled, true); // top, left to right
    travelled += sideW;
    {
        const Color from = at(travelled), to = at(travelled + arc), middle = at(travelled + arc * 0.5f);
        emitQuad(id, Rect(dst.right() - b, dst.y, b, b), Rect(1.0f - ub, 0.0f, ub, vb), from, middle, middle, to);
    }
    travelled += arc;
    if (sideH > 0.0f) vertical(dst.right() - b, 1.0f - ub, travelled, true); // right, downwards
    travelled += sideH;
    {
        const Color from = at(travelled), to = at(travelled + arc), middle = at(travelled + arc * 0.5f);
        emitQuad(id, Rect(dst.right() - b, dst.bottom() - b, b, b), Rect(1.0f - ub, 1.0f - vb, ub, vb), middle, from,
                 to, middle);
    }
    travelled += arc;
    if (sideW > 0.0f) horizontal(dst.bottom() - b, 1.0f - vb, travelled, false); // bottom, right to left
    travelled += sideW;
    {
        const Color from = at(travelled), to = at(travelled + arc), middle = at(travelled + arc * 0.5f);
        emitQuad(id, Rect(dst.x, dst.bottom() - b, b, b), Rect(0.0f, 1.0f - vb, ub, vb), to, middle, middle, from);
    }
    travelled += arc;
    if (sideH > 0.0f) vertical(dst.x, 0.0f, travelled, false); // left, upwards
    travelled += sideH;
    {
        const Color from = at(travelled), to = at(travelled + arc), middle = at(travelled + arc * 0.5f);
        emitQuad(id, Rect(dst.x, dst.y, b, b), Rect(0.0f, 0.0f, ub, vb), middle, to, from, middle);
    }
}

void Renderer::buttonGlyph(ButtonGlyph glyph, const Rect& dst, const Color& color)
{
    const int size = std::max(8, static_cast<int>(std::lround(std::max(dst.w, dst.h))));
    const uint64_t key = kMaskGlyph | (static_cast<uint64_t>(glyph) << 16) | static_cast<uint64_t>(size & 0xFFFF);
    const Texture& texture = mask(key, paintMask);
    emitQuad(texture.id(),
             Rect(std::floor(dst.x), std::floor(dst.y), static_cast<float>(size), static_cast<float>(size)), kFullUv,
             color, color, color, color);
}

void Renderer::image(const Texture& texture, const Rect& dst, const Rect& uv, const Color& tint)
{
    if (texture.valid()) {
        emitQuad(texture.id(), dst, uv, tint, tint, tint, tint);
    }
}

void Renderer::imageCover(const Texture& texture, const Rect& dst, const Color& tint)
{
    if (!texture.valid() || dst.empty()) {
        return;
    }
    const float scale = std::max(dst.w / texture.width(), dst.h / texture.height());
    const float shownW = std::min(1.0f, dst.w / (texture.width() * scale));
    const float shownH = std::min(1.0f, dst.h / (texture.height() * scale));
    image(texture, dst, Rect((1.0f - shownW) * 0.5f, (1.0f - shownH) * 0.5f, shownW, shownH), tint);
}

float Renderer::text(const std::string& str, float x, float baseline, int size, const Color& color, float maxWidth)
{
    const float startX = std::floor(x);
    const float baseY = std::floor(baseline);
    float pen = startX;
    for (size_t i = 0; i < str.size();) {
        const Glyph& glyph = font_.glyph(nextCodepoint(str, i), size);
        if (pen + glyph.advance > startX + maxWidth) {
            break;
        }
        if (glyph.texture) {
            const Rect dst(pen + glyph.offsetX, baseY + glyph.offsetY, static_cast<float>(glyph.width),
                           static_cast<float>(glyph.height));
            const Rect uv(glyph.u0, glyph.v0, glyph.u1 - glyph.u0, glyph.v1 - glyph.v0);
            emitQuad(glyph.texture, dst, uv, color, color, color, color);
        }
        pen += glyph.advance;
    }
    return pen - startX;
}

float Renderer::measureText(const std::string& str, int size)
{
    float width = 0.0f;
    for (size_t i = 0; i < str.size();) {
        width += font_.glyph(nextCodepoint(str, i), size).advance;
    }
    return width;
}

float Renderer::capHeight(int size)
{
    return static_cast<float>(font_.glyph('H', size).height);
}

float Renderer::textFit(const std::string& str, float x, float baseline, int size, const Color& color, float maxWidth)
{
    if (measureText(str, size) <= maxWidth) {
        return text(str, x, baseline, size, color, maxWidth);
    }
    const float dots = measureText("...", size);
    float used = 0.0f;
    size_t end = 0;
    while (end < str.size()) {
        size_t next = end;
        const float advance = static_cast<float>(font_.glyph(nextCodepoint(str, next), size).advance);
        if (used + advance + dots > maxWidth) {
            break;
        }
        used += advance;
        end = next;
    }
    std::string shown = str.substr(0, end);
    while (!shown.empty() && shown.back() == ' ') {
        shown.pop_back();
    }
    return text(shown + "...", x, baseline, size, color, maxWidth + 1.0f);
}

float Renderer::textCentered(const std::string& str, float centerX, float baseline, int size, const Color& color)
{
    const float width = measureText(str, size);
    return text(str, centerX - width * 0.5f, baseline, size, color, width + 2.0f);
}

float Renderer::textRight(const std::string& str, float right, float baseline, int size, const Color& color)
{
    const float width = measureText(str, size);
    return text(str, right - width, baseline, size, color, width + 2.0f);
}

void Renderer::textLines(const std::string& str, float x, float baseline, int size, const Color& color, float width,
                         int maxLines)
{
    std::string rest = str;
    for (int line = 0; line < maxLines && !rest.empty(); ++line) {
        float used = 0.0f;
        size_t end = 0;
        size_t lastSpace = 0;
        while (end < rest.size()) {
            size_t next = end;
            const float advance = static_cast<float>(font_.glyph(nextCodepoint(rest, next), size).advance);
            if (used + advance > width) {
                break;
            }
            used += advance;
            if (rest[end] == ' ') {
                lastSpace = end;
            }
            end = next;
        }
        if (end == 0) {
            break;
        }
        if (end < rest.size() && lastSpace) {
            end = lastSpace; // break at the last word boundary that fits
        }
        text(rest.substr(0, end), x, baseline + static_cast<float>(line * (size + 5)), size, color, width);
        rest = rest.substr(end);
        while (!rest.empty() && rest[0] == ' ') {
            rest.erase(0, 1);
        }
    }
}

} // namespace ns
