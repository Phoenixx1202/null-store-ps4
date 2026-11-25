// Software implementation of the render backend for the PC preview tool
// (tools/preview.cpp). It rasterises the very quads the PS4 sends to Piglet,
// with the same premultiplied blending, bilinear filtering and scissor, so a
// preview shows what the layout code produces without a console.
#include "graphics/RenderBackend.h"

#include <cstring>
#include <unordered_map>

namespace ns {

namespace {

struct SoftTexture {
    int width = 0;
    int height = 0;
    bool linear = true;
    std::vector<uint8_t> pixels;
};

class SoftwareBackend final : public RenderBackend {
public:
    const char* name() const override { return "software"; }

    bool init(int width, int height) override
    {
        width_ = width;
        height_ = height;
        frame_.assign(static_cast<size_t>(width) * height * 4, 0);
        return true;
    }

    void shutdown() override { textures_.clear(); }

    TextureId createTexture(int width, int height, const uint8_t* rgba, bool linearFilter) override
    {
        if (width <= 0 || height <= 0 || !rgba) {
            return 0;
        }
        SoftTexture texture;
        texture.width = width;
        texture.height = height;
        texture.linear = linearFilter;
        texture.pixels.assign(rgba, rgba + static_cast<size_t>(width) * height * 4);
        const TextureId id = ++nextId_;
        textures_.emplace(id, std::move(texture));
        return id;
    }

    bool updateTexture(TextureId id, int width, int height, const uint8_t* rgba) override
    {
        const auto it = textures_.find(id);
        if (it == textures_.end() || it->second.width != width || it->second.height != height) {
            return false;
        }
        memcpy(it->second.pixels.data(), rgba, it->second.pixels.size());
        return true;
    }

    void destroyTexture(TextureId id) override { textures_.erase(id); }

    void beginFrame(const Color& clear) override
    {
        const uint8_t pixel[4] = { toByte(clear.r), toByte(clear.g), toByte(clear.b), 255 };
        for (size_t i = 0; i < frame_.size(); i += 4) {
            memcpy(&frame_[i], pixel, 4);
        }
    }

    void draw(const Vertex* vertices, size_t vertexCount, const DrawCommand* commands,
              size_t commandCount) override
    {
        for (size_t c = 0; c < commandCount; ++c) {
            const DrawCommand& command = commands[c];
            const auto found = textures_.find(command.texture);
            if (found == textures_.end()) {
                continue;
            }
            for (uint32_t q = 0; q < command.quadCount; ++q) {
                const size_t first = (static_cast<size_t>(command.firstQuad) + q) * 4;
                if (first + 3 < vertexCount) {
                    drawQuad(found->second, vertices + first, command.clip);
                }
            }
        }
    }

    bool present() override { return true; }

    bool capture(std::vector<uint8_t>& rgba) override
    {
        rgba = frame_;
        return true;
    }

private:
    static uint8_t toByte(float v)
    {
        return static_cast<uint8_t>(clampf(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    }

    static void sample(const SoftTexture& t, float u, float v, float out[4])
    {
        // GL_CLAMP_TO_EDGE with texel centres at +0.5.
        const float fx = clampf(u * t.width - 0.5f, 0.0f, static_cast<float>(t.width - 1));
        const float fy = clampf(v * t.height - 0.5f, 0.0f, static_cast<float>(t.height - 1));
        if (!t.linear) {
            const uint8_t* p = &t.pixels[(static_cast<size_t>(fy + 0.5f) * t.width + static_cast<size_t>(fx + 0.5f)) * 4];
            for (int i = 0; i < 4; ++i) out[i] = p[i];
            return;
        }
        const int x0 = static_cast<int>(fx);
        const int y0 = static_cast<int>(fy);
        const int x1 = std::min(t.width - 1, x0 + 1);
        const int y1 = std::min(t.height - 1, y0 + 1);
        const float tx = fx - x0;
        const float ty = fy - y0;
        const uint8_t* p00 = &t.pixels[(static_cast<size_t>(y0) * t.width + x0) * 4];
        const uint8_t* p10 = &t.pixels[(static_cast<size_t>(y0) * t.width + x1) * 4];
        const uint8_t* p01 = &t.pixels[(static_cast<size_t>(y1) * t.width + x0) * 4];
        const uint8_t* p11 = &t.pixels[(static_cast<size_t>(y1) * t.width + x1) * 4];
        for (int i = 0; i < 4; ++i) {
            const float top = p00[i] * (1 - tx) + p10[i] * tx;
            const float bottom = p01[i] * (1 - tx) + p11[i] * tx;
            out[i] = top * (1 - ty) + bottom * ty;
        }
    }

    // Quads are axis-aligned: v[0] top-left, v[1] top-right, v[2] bottom-left,
    // v[3] bottom-right. Pixels are covered when their centre is inside.
    void drawQuad(const SoftTexture& texture, const Vertex* v, const ClipRect& clip)
    {
        const float left = v[0].x, top = v[0].y, right = v[3].x, bottom = v[3].y;
        if (right <= left || bottom <= top) {
            return;
        }
        int x0 = std::max(0, static_cast<int>(std::ceil(left - 0.5f)));
        int y0 = std::max(0, static_cast<int>(std::ceil(top - 0.5f)));
        int x1 = std::min(width_, static_cast<int>(std::ceil(right - 0.5f)));
        int y1 = std::min(height_, static_cast<int>(std::ceil(bottom - 0.5f)));
        if (clip.enabled) {
            x0 = std::max(x0, clip.x);
            y0 = std::max(y0, clip.y);
            x1 = std::min(x1, clip.x + clip.w);
            y1 = std::min(y1, clip.y + clip.h);
        }
        const bool flat = memcmp(&v[0].r, &v[1].r, 4) == 0 && memcmp(&v[0].r, &v[2].r, 4) == 0 &&
                          memcmp(&v[0].r, &v[3].r, 4) == 0;
        for (int y = y0; y < y1; ++y) {
            const float ty = (y + 0.5f - top) / (bottom - top);
            for (int x = x0; x < x1; ++x) {
                const float tx = (x + 0.5f - left) / (right - left);
                float texel[4];
                sample(texture, v[0].u + (v[3].u - v[0].u) * tx, v[0].v + (v[3].v - v[0].v) * ty, texel);
                float tint[4];
                const uint8_t* c0 = &v[0].r;
                if (flat) {
                    for (int i = 0; i < 4; ++i) tint[i] = c0[i] / 255.0f;
                } else {
                    const uint8_t* c1 = &v[1].r;
                    const uint8_t* c2 = &v[2].r;
                    const uint8_t* c3 = &v[3].r;
                    for (int i = 0; i < 4; ++i) {
                        const float upper = c0[i] * (1 - tx) + c1[i] * tx;
                        const float lower = c2[i] * (1 - tx) + c3[i] * tx;
                        tint[i] = (upper * (1 - ty) + lower * ty) / 255.0f;
                    }
                }
                // Premultiplied source over destination: ONE, ONE_MINUS_SRC_ALPHA.
                const float alpha = texel[3] * tint[3] / 255.0f;
                uint8_t* dst = &frame_[(static_cast<size_t>(y) * width_ + x) * 4];
                for (int i = 0; i < 3; ++i) {
                    const float value = texel[i] * tint[i] + dst[i] * (1.0f - alpha);
                    dst[i] = static_cast<uint8_t>(std::min(255.0f, value + 0.5f));
                }
                dst[3] = 255;
            }
        }
    }

    int width_ = 0;
    int height_ = 0;
    TextureId nextId_ = 0;
    std::vector<uint8_t> frame_;
    std::unordered_map<TextureId, SoftTexture> textures_;
};

} // namespace

std::unique_ptr<RenderBackend> createRenderBackend()
{
    return std::make_unique<SoftwareBackend>();
}

} // namespace ns
