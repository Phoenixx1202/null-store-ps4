#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "graphics/Texture.h"

// -----------------------------------------------------------------------------
// Text with stb_truetype and a glyph atlas.
//
// Each (code point, pixel size) pair is rasterised once into a white coverage
// mask on an atlas page; drawing a string is then one tinted quad per glyph in
// the same batch as everything else. Pages are plain textures, uploaded whole
// when they received new glyphs (at most once per frame), so labels that change
// every frame, like transfer statistics, never create textures.
// -----------------------------------------------------------------------------
namespace ns {

struct Glyph {
    TextureId texture = 0;
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    int width = 0, height = 0; // bitmap size in pixels (0 for spaces)
    int offsetX = 0, offsetY = 0; // from the pen position on the baseline to the bitmap's top-left
    int advance = 0;
};

class FontAtlas {
public:
    FontAtlas();
    ~FontAtlas();

    FontAtlas(const FontAtlas&) = delete;
    FontAtlas& operator=(const FontAtlas&) = delete;

    bool load(RenderBackend& backend, const std::string& path);
    // A second font for the letters the first one lacks (ğ ş İ of Turkish).
    bool loadFallback(const std::string& path);
    bool valid() const { return info_ != nullptr; }

    const Glyph& glyph(uint32_t codepoint, int pixelSize);

    // Uploads the pages that changed since the last call. The Renderer calls
    // this before it submits a frame.
    void uploadPending();

    // Frees the pages (and forgets their glyphs) before the backend goes away.
    void releaseTextures()
    {
        glyphs_.clear();
        pages_.clear();
    }

    int pageCount() const { return static_cast<int>(pages_.size()); }

private:
    struct Page {
        Image pixels;
        Texture texture;
        int cursorX = 1;
        int cursorY = 1;
        int rowHeight = 0;
        bool dirty = false;
    };

    bool place(int width, int height, int& page, int& x, int& y);

    RenderBackend* backend_ = nullptr;
    std::vector<uint8_t> data_;
    void* info_ = nullptr; // stbtt_fontinfo*
    std::vector<uint8_t> fallbackData_;
    void* fallback_ = nullptr; // stbtt_fontinfo*, may stay null
    std::vector<std::unique_ptr<Page>> pages_;
    std::unordered_map<uint64_t, Glyph> glyphs_;
};

// Decodes one UTF-8 sequence starting at `index` and advances it. Invalid
// bytes become U+FFFD.
uint32_t nextCodepoint(const std::string& text, size_t& index);

} // namespace ns
