#include "graphics/Font.h"

#include <cstring>

#include "core/Log.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb/stb_truetype.h"
#pragma clang diagnostic pop

namespace ns {

namespace {

constexpr int kPageSize = 1024;
constexpr int kPadding = 1; // transparent border so bilinear sampling never bleeds

stbtt_fontinfo* fontInfo(void* info)
{
    return static_cast<stbtt_fontinfo*>(info);
}

} // namespace

uint32_t nextCodepoint(const std::string& text, size_t& index)
{
    const auto byteAt = [&](size_t i) { return static_cast<unsigned char>(text[i]); };
    const uint32_t lead = byteAt(index++);
    if (lead < 0x80) {
        return lead;
    }
    int extra = 0;
    uint32_t codepoint = 0;
    if (lead >= 0xC2 && lead <= 0xDF) {
        extra = 1;
        codepoint = lead & 0x1F;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        extra = 2;
        codepoint = lead & 0x0F;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        extra = 3;
        codepoint = lead & 0x07;
    } else {
        return 0xFFFD;
    }
    for (int i = 0; i < extra; ++i) {
        if (index >= text.size() || (byteAt(index) & 0xC0) != 0x80) {
            return 0xFFFD;
        }
        codepoint = (codepoint << 6) | (byteAt(index++) & 0x3F);
    }
    return codepoint;
}

FontAtlas::FontAtlas() = default;

FontAtlas::~FontAtlas()
{
    delete fontInfo(info_);
    delete fontInfo(fallback_);
}

bool FontAtlas::loadFallback(const std::string& path)
{
    if (!readBinaryFile(path, fallbackData_)) {
        return false;
    }
    auto* info = new stbtt_fontinfo;
    if (!stbtt_InitFont(info, fallbackData_.data(), stbtt_GetFontOffsetForIndex(fallbackData_.data(), 0))) {
        delete info;
        return false;
    }
    fallback_ = info;
    return true;
}

bool FontAtlas::load(RenderBackend& backend, const std::string& path)
{
    backend_ = &backend;
    if (!readBinaryFile(path, data_)) {
        NS_LOG_ERROR("Font missing: %s", path.c_str());
        return false;
    }
    auto* info = new stbtt_fontinfo;
    if (!stbtt_InitFont(info, data_.data(), stbtt_GetFontOffsetForIndex(data_.data(), 0))) {
        NS_LOG_ERROR("Font unreadable: %s", path.c_str());
        delete info;
        return false;
    }
    info_ = info;
    return true;
}

bool FontAtlas::place(int width, int height, int& pageIndex, int& x, int& y)
{
    const int needW = width + kPadding;
    const int needH = height + kPadding;
    if (needW + 1 > kPageSize || needH + 1 > kPageSize) {
        return false;
    }
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!pages_.empty()) {
            Page& page = *pages_.back();
            if (page.cursorX + needW > kPageSize) { // next shelf
                page.cursorX = 1;
                page.cursorY += page.rowHeight;
                page.rowHeight = 0;
            }
            if (page.cursorY + needH <= kPageSize) {
                pageIndex = static_cast<int>(pages_.size()) - 1;
                x = page.cursorX;
                y = page.cursorY;
                page.cursorX += needW;
                page.rowHeight = std::max(page.rowHeight, needH);
                return true;
            }
        }
        // The last page is full (or there is none yet): start another one.
        auto page = std::make_unique<Page>();
        page->pixels.reset(kPageSize, kPageSize);
        if (!page->texture.create(*backend_, page->pixels, true)) {
            NS_LOG_ERROR("Font atlas page %d could not be created", static_cast<int>(pages_.size()));
            return false;
        }
        pages_.push_back(std::move(page));
    }
    return false;
}

const Glyph& FontAtlas::glyph(uint32_t codepoint, int pixelSize)
{
    const uint64_t key = (static_cast<uint64_t>(codepoint) << 16) | static_cast<uint32_t>(pixelSize & 0xFFFF);
    const auto found = glyphs_.find(key);
    if (found != glyphs_.end()) {
        return found->second;
    }

    Glyph glyph;
    if (info_ && pixelSize > 0) {
        stbtt_fontinfo* info = fontInfo(info_);
        if (codepoint >= 128 && stbtt_FindGlyphIndex(info, static_cast<int>(codepoint)) == 0) {
            // Not in the font: from the second one, or left out rather than an empty box.
            stbtt_fontinfo* other = fontInfo(fallback_);
            if (!other || stbtt_FindGlyphIndex(other, static_cast<int>(codepoint)) == 0) {
                return glyphs_.emplace(key, glyph).first->second;
            }
            info = other;
        }
        const float scale = stbtt_ScaleForPixelHeight(info, static_cast<float>(pixelSize));
        int advance = 0;
        int bearing = 0;
        stbtt_GetCodepointHMetrics(info, static_cast<int>(codepoint), &advance, &bearing);
        glyph.advance = static_cast<int>(advance * scale);

        int w = 0, h = 0, offsetX = 0, offsetY = 0;
        unsigned char* bitmap =
            stbtt_GetCodepointBitmap(info, 0, scale, static_cast<int>(codepoint), &w, &h, &offsetX, &offsetY);
        if (bitmap && w > 0 && h > 0) {
            int pageIndex = 0, x = 0, y = 0;
            if (place(w, h, pageIndex, x, y)) {
                Page& page = *pages_[pageIndex];
                for (int row = 0; row < h; ++row) {
                    uint8_t* dst = page.pixels.data() + (static_cast<size_t>(y + row) * kPageSize + x) * 4;
                    const unsigned char* src = bitmap + static_cast<size_t>(row) * w;
                    for (int column = 0; column < w; ++column, dst += 4) {
                        dst[0] = dst[1] = dst[2] = dst[3] = src[column]; // premultiplied white
                    }
                }
                page.dirty = true;
                glyph.texture = page.texture.id();
                glyph.u0 = static_cast<float>(x) / kPageSize;
                glyph.v0 = static_cast<float>(y) / kPageSize;
                glyph.u1 = static_cast<float>(x + w) / kPageSize;
                glyph.v1 = static_cast<float>(y + h) / kPageSize;
                glyph.width = w;
                glyph.height = h;
                glyph.offsetX = offsetX;
                glyph.offsetY = offsetY;
            }
        }
        if (bitmap) {
            stbtt_FreeBitmap(bitmap, nullptr);
        }
    }
    return glyphs_.emplace(key, glyph).first->second;
}

void FontAtlas::uploadPending()
{
    for (auto& page : pages_) {
        if (page->dirty) {
            page->texture.update(page->pixels);
            page->dirty = false;
        }
    }
}

} // namespace ns
