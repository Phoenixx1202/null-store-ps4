#pragma once

#include "graphics/Image.h"
#include "graphics/RenderBackend.h"

namespace ns {

// RAII handle to a backend texture. Only used from the render thread.
class Texture {
public:
    Texture() = default;
    ~Texture() { destroy(); }

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    Texture(Texture&& other) noexcept
        : backend_(other.backend_), id_(other.id_), width_(other.width_), height_(other.height_)
    {
        other.id_ = 0;
    }

    Texture& operator=(Texture&& other) noexcept
    {
        if (this != &other) {
            destroy();
            backend_ = other.backend_;
            id_ = other.id_;
            width_ = other.width_;
            height_ = other.height_;
            other.id_ = 0;
        }
        return *this;
    }

    bool create(RenderBackend& backend, const Image& image, bool linearFilter = true)
    {
        destroy();
        if (image.empty()) {
            return false;
        }
        backend_ = &backend;
        id_ = backend.createTexture(image.width(), image.height(), image.data(), linearFilter);
        width_ = id_ ? image.width() : 0;
        height_ = id_ ? image.height() : 0;
        return id_ != 0;
    }

    // Replaces all pixels; the image must have the size the texture was created with.
    bool update(const Image& image)
    {
        return id_ && image.width() == width_ && image.height() == height_ &&
               backend_->updateTexture(id_, width_, height_, image.data());
    }

    void destroy()
    {
        if (id_ && backend_) {
            backend_->destroyTexture(id_);
        }
        id_ = 0;
        width_ = 0;
        height_ = 0;
    }

    TextureId id() const { return id_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool valid() const { return id_ != 0; }

private:
    RenderBackend* backend_ = nullptr;
    TextureId id_ = 0;
    int width_ = 0;
    int height_ = 0;
};

} // namespace ns
