#pragma once

#include <memory>
#include <string>

#include "graphics/GL.h"

// -----------------------------------------------------------------------------
// Owns the GL context and its presentation surface: loads the Piglet module,
// configures it and creates the EGL window surface
// (src/platform/ps4/GLContextPs4.cpp).
// -----------------------------------------------------------------------------
namespace ns {

class GLContext {
public:
    GLContext();
    ~GLContext();

    GLContext(const GLContext&) = delete;
    GLContext& operator=(const GLContext&) = delete;

    bool init(int width, int height);
    void shutdown();
    bool swapBuffers();
    bool valid() const;

    int width() const { return width_; }
    int height() const { return height_; }
    const std::string& rendererName() const { return rendererName_; }

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
    int width_ = 0;
    int height_ = 0;
    std::string rendererName_;
};

} // namespace ns
