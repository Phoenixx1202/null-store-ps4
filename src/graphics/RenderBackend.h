#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/Geometry.h"

// -----------------------------------------------------------------------------
// The device the Renderer draws on.
//
// The Renderer turns every primitive into textured quads with premultiplied
// vertex colours and hands a whole frame to a backend:
//   PS4:  Piglet (OpenGL ES 2.0 over EGL), src/graphics/GLBackend.cpp
//   Host: a software rasteriser that writes PNG previews,
//         src/platform/host/SoftwareBackend.cpp
// -----------------------------------------------------------------------------
namespace ns {

using TextureId = uint32_t; // 0 = none

struct Vertex {
    float x, y;         // pixels, origin at the top-left of the screen
    float u, v;         // normalised texture coordinates
    uint8_t r, g, b, a; // premultiplied tint
};

struct ClipRect {
    bool enabled = false;
    int x = 0, y = 0, w = 0, h = 0; // pixels, origin at the top-left

    bool operator==(const ClipRect& o) const
    {
        return enabled == o.enabled && (!enabled || (x == o.x && y == o.y && w == o.w && h == o.h));
    }
    bool operator!=(const ClipRect& o) const { return !(*this == o); }
};

// A run of consecutive quads (four vertices each) sharing texture and clip.
struct DrawCommand {
    TextureId texture = 0;
    uint32_t firstQuad = 0;
    uint32_t quadCount = 0;
    ClipRect clip;
};

class RenderBackend {
public:
    // Quads per draw() call: 4 * 4096 vertices fit 16-bit indices.
    static constexpr size_t kMaxQuads = 4096;

    virtual ~RenderBackend() = default;

    virtual bool init(int width, int height) = 0;
    virtual void shutdown() = 0;
    virtual const char* name() const = 0;

    // Pixels are RGBA8 with premultiplied alpha, rows top to bottom.
    virtual TextureId createTexture(int width, int height, const uint8_t* rgba, bool linearFilter) = 0;
    // Replaces every pixel of a texture created with the same size.
    virtual bool updateTexture(TextureId texture, int width, int height, const uint8_t* rgba) = 0;
    virtual void destroyTexture(TextureId texture) = 0;

    virtual void beginFrame(const Color& clearColor) = 0;
    virtual void draw(const Vertex* vertices, size_t vertexCount, const DrawCommand* commands,
                      size_t commandCount) = 0;
    virtual bool present() = 0;

    // Top-down RGBA8 copy of the finished frame (previews and screenshots).
    virtual bool capture(std::vector<uint8_t>& rgba) = 0;
};

// Implemented once per platform.
std::unique_ptr<RenderBackend> createRenderBackend();

} // namespace ns
