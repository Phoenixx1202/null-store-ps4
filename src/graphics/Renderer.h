#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Geometry.h"
#include "graphics/Font.h"
#include "graphics/Image.h"
#include "graphics/RenderBackend.h"
#include "graphics/Texture.h"

// -----------------------------------------------------------------------------
// 2D renderer.
//
// Everything on screen is a textured quad with premultiplied alpha. Solid
// colours, gradients and text colours are vertex tints of white masks, so a
// colour change never creates a texture. The quads of a whole frame are
// collected and handed to the backend in one go: one draw call per
// (texture, clip) run.
//
// Coordinates are pixels on the 1920x1080 surface, origin at the top-left.
// -----------------------------------------------------------------------------
namespace ns {

class Renderer {
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool init(std::unique_ptr<RenderBackend> backend, int width, int height, const std::string& fontPath);
    void shutdown();

    int width() const { return width_; }
    int height() const { return height_; }
    uint64_t frame() const { return frame_; }
    RenderBackend& backend() { return *backend_; }

    void beginFrame(const Color& clearColor);
    // Submits the frame and shows it.
    bool endFrame();
    // Top-down RGBA8 copy of the last frame.
    bool capture(std::vector<uint8_t>& rgba) { return backend_->capture(rgba); }

    // Shapes ----------------------------------------------------------------------
    void rect(const Rect& dst, const Color& color);
    void gradientVertical(const Rect& dst, const Color& top, const Color& bottom);
    void gradientHorizontal(const Rect& dst, const Color& left, const Color& right);
    // One colour per corner: top-left, top-right, bottom-left, bottom-right.
    void rectColors(const Rect& dst, const Color& topLeft, const Color& topRight, const Color& bottomLeft,
                    const Color& bottomRight);
    void roundedRect(const Rect& dst, float radius, const Color& color);
    void roundedOutline(const Rect& dst, float radius, float thickness, const Color& color);
    // Outline whose colour changes along the way round: `colorAt(t)` gives it at
    // t in [0, 1), clockwise from the left end of the top edge.
    void roundedOutlineTraced(const Rect& dst, float radius, float thickness,
                              const std::function<Color(float)>& colorAt);
    void buttonGlyph(ButtonGlyph glyph, const Rect& dst, const Color& color);

    // Pictures --------------------------------------------------------------------
    // `uv` is the normalised part of the texture to show; `tint` multiplies it.
    void image(const Texture& texture, const Rect& dst, const Rect& uv = Rect(0, 0, 1, 1),
               const Color& tint = Color(1, 1, 1, 1));
    // Fills `dst` with the texture, keeping its proportions and cropping the
    // overflow around the centre.
    void imageCover(const Texture& texture, const Rect& dst, const Color& tint = Color(1, 1, 1, 1));

    // Text ------------------------------------------------------------------------
    // `baseline` is the y of the text's baseline. Glyphs that would pass
    // x + maxWidth are not drawn. Returns the width drawn.
    float text(const std::string& text, float x, float baseline, int size, const Color& color,
               float maxWidth = 1800.0f);
    // Word-wrapped text of at most `maxLines` lines, `size + 5` pixels apart.
    void textLines(const std::string& text, float x, float baseline, int size, const Color& color, float width,
                   int maxLines);
    float measureText(const std::string& text, int size);
    // Single line that ends in "..." when it does not fit `maxWidth`.
    float textFit(const std::string& text, float x, float baseline, int size, const Color& color, float maxWidth);
    // Single line centred on `centerX`, or ending at `right`.
    float textCentered(const std::string& text, float centerX, float baseline, int size, const Color& color);
    float textRight(const std::string& text, float right, float baseline, int size, const Color& color);
    // Height of a capital letter: the baseline that centres text on `centerY`
    // is centerY + capHeight(size) / 2.
    float capHeight(int size);

    // Clipping and group opacity ----------------------------------------------------
    void pushClip(const Rect& rect);
    void popClip();
    void pushOpacity(float opacity);
    void popOpacity();

private:
    struct CachedMask {
        Texture texture;
        uint64_t lastUsedFrame = 0;
    };

    // Corner order: top-left, top-right, bottom-left, bottom-right.
    void emitQuad(TextureId texture, const Rect& dst, const Rect& uv, const Color& c0, const Color& c1,
                  const Color& c2, const Color& c3);
    void nineSlice(const Texture& texture, const Rect& dst, float border, const Color& tint);
    const Texture& mask(uint64_t key, Image (*paint)(uint64_t key));
    void flush();
    void updateClip();
    float opacity() const { return opacityStack_.empty() ? 1.0f : opacityStack_.back(); }

    std::unique_ptr<RenderBackend> backend_;
    FontAtlas font_;
    Texture white_;
    std::unordered_map<uint64_t, CachedMask> masks_;

    std::vector<Vertex> vertices_;
    std::vector<DrawCommand> commands_;
    std::vector<Rect> clipStack_;
    std::vector<float> opacityStack_;
    ClipRect clip_;

    int width_ = 0;
    int height_ = 0;
    uint64_t frame_ = 0;
    bool ready_ = false;
};

} // namespace ns
