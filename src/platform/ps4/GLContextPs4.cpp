// Piglet (OpenGL ES 2.0 / EGL 1.4) context for the PS4.
//
// Sequence and configuration follow the OpenOrbis "piglet" sample (itself
// derived from flatz's ps4_gl_test); the same values are used by other Piglet
// homebrew on retail firmware:
//   - scePigletSetConfigurationVSH must run once per process, before EGL.
//   - Piglet only accepts 1920x1080 window surfaces.
//   - eglTerminate breaks a later re-initialisation, so shutdown is final.
//   - The title must be of category "gde" (param.sfo): Piglet presents
//     through the system compositor, and in a game title ("gd")
//     eglGetDisplay returns EGL_NO_DISPLAY.
#include "graphics/GLContext.h"

#include <cstdio>
#include <cstring>

#include <orbis/libkernel.h>

#include "core/Log.h"
#include "platform/ps4/Ps4System.h"

namespace ns {

namespace {

constexpr uint64_t kSystemSharedMemory = 250ull * 1024 * 1024;
constexpr uint64_t kVideoSharedMemory = 256ull * 1024 * 1024;
constexpr uint64_t kMaxMappedFlexibleMemory = 170ull * 1024 * 1024;
constexpr uint32_t kDrawCommandBufferSize = 1 * 1024 * 1024;
constexpr uint32_t kLcueResourceBufferSize = 1 * 1024 * 1024;

} // namespace

namespace ps4 {

int32_t loadSystemModule(const char* fileName)
{
    char paths[2][512];
    int count = 0;
    if (const char* sandbox = sceKernelGetFsSandboxRandomWord()) {
        snprintf(paths[count++], sizeof(paths[0]), "/%s/common/lib/%s", sandbox, fileName);
    }
    snprintf(paths[count++], sizeof(paths[0]), "/system/common/lib/%s", fileName);

    int32_t handle = -1;
    for (int i = 0; i < count; ++i) {
        int32_t startResult = 0;
        handle = static_cast<int32_t>(sceKernelLoadStartModule(paths[i], 0, nullptr, 0, nullptr, &startResult));
        if (handle >= 0) {
            NS_LOG_INFO("Module loaded: %s (handle=0x%X, start=%d)", paths[i], handle, startResult);
            return handle;
        }
        NS_LOG_WARN("sceKernelLoadStartModule(%s) failed: 0x%08X", paths[i], static_cast<unsigned>(handle));
    }
    NS_LOG_ERROR("Unable to load module %s", fileName);
    return handle;
}

} // namespace ps4

struct GLContext::Impl {
    int32_t pigletModule = -1;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    EGLint lastSwapError = EGL_SUCCESS;
};

GLContext::GLContext() : impl_(std::make_unique<Impl>()) {}

GLContext::~GLContext()
{
    shutdown();
}

bool GLContext::valid() const
{
    return impl_ && impl_->context != EGL_NO_CONTEXT;
}

bool GLContext::init(int width, int height)
{
    width_ = width;
    height_ = height;
    Impl& s = *impl_;

    s.pigletModule = ps4::loadSystemModule("libScePigletv2VSH.sprx");
    if (s.pigletModule < 0) {
        return false;
    }

    OrbisPglConfig config;
    memset(&config, 0, sizeof(config));
    config.size = sizeof(config);
    config.flags = ORBIS_PGL_FLAGS_USE_COMPOSITE_EXT | ORBIS_PGL_FLAGS_USE_FLEXIBLE_MEMORY | 0x60;
    config.processOrder = 1;
    config.systemSharedMemorySize = kSystemSharedMemory;
    config.videoSharedMemorySize = kVideoSharedMemory;
    config.maxMappedFlexibleMemory = kMaxMappedFlexibleMemory;
    config.drawCommandBufferSize = kDrawCommandBufferSize;
    config.lcueResourceBufferSize = kLcueResourceBufferSize;
    config.dbgPosCmd_0x40 = static_cast<uint32_t>(width);
    config.dbgPosCmd_0x44 = static_cast<uint32_t>(height);
    config.dbgPosCmd_0x48 = 0;
    config.dbgPosCmd_0x4C = 0;
    config.unk_0x5C = 2;
    if (!scePigletSetConfigurationVSH(&config)) {
        NS_LOG_ERROR("scePigletSetConfigurationVSH failed");
        return false;
    }
    NS_LOG_INFO("Piglet configured (flags=0x%X, direct memory pool %zu MB)", config.flags,
                sceKernelGetDirectMemorySize() / (1024 * 1024));

    s.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (s.display == EGL_NO_DISPLAY) {
        NS_LOG_ERROR("eglGetDisplay failed: 0x%04X (is the title's category \"gde\"?)", eglGetError());
        return false;
    }
    EGLint major = 0;
    EGLint minor = 0;
    if (!eglInitialize(s.display, &major, &minor)) {
        NS_LOG_ERROR("eglInitialize failed: 0x%04X", eglGetError());
        return false;
    }
    NS_LOG_INFO("EGL initialized: version %d.%d", major, minor);

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        NS_LOG_ERROR("eglBindAPI failed: 0x%04X", eglGetError());
        return false;
    }

    // Swap interval 0, like the OpenOrbis sample and flatz's test: with
    // interval 1 eglSwapBuffers has been seen to block forever on retail
    // consoles. The application paces its own frames and the compositor shows
    // the latest finished buffer, so there is no tearing.
    if (!eglSwapInterval(s.display, 0)) {
        NS_LOG_WARN("eglSwapInterval(0) failed: 0x%04X", eglGetError());
    }

    const EGLint attribs[] = {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0,
        EGL_STENCIL_SIZE, 0,
        EGL_SAMPLE_BUFFERS, 0,
        EGL_SAMPLES, 0,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_NONE,
    };
    EGLConfig eglConfig = nullptr;
    EGLint numConfigs = 0;
    if (!eglChooseConfig(s.display, attribs, &eglConfig, 1, &numConfigs) || numConfigs < 1) {
        NS_LOG_ERROR("eglChooseConfig failed: 0x%04X (configs=%d)", eglGetError(), numConfigs);
        return false;
    }

    OrbisPglWindow window;
    memset(&window, 0, sizeof(window));
    window.uID = 0;
    window.uWidth = static_cast<khronos_uint32_t>(width);
    window.uHeight = static_cast<khronos_uint32_t>(height);
    const EGLint windowAttribs[] = { EGL_RENDER_BUFFER, EGL_BACK_BUFFER, EGL_NONE };
    s.surface = eglCreateWindowSurface(s.display, eglConfig, &window, windowAttribs);
    if (s.surface == EGL_NO_SURFACE) {
        NS_LOG_ERROR("eglCreateWindowSurface failed: 0x%04X", eglGetError());
        return false;
    }

    const EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    s.context = eglCreateContext(s.display, eglConfig, EGL_NO_CONTEXT, contextAttribs);
    if (s.context == EGL_NO_CONTEXT) {
        NS_LOG_ERROR("eglCreateContext failed: 0x%04X", eglGetError());
        return false;
    }
    if (!eglMakeCurrent(s.display, s.surface, s.surface, s.context)) {
        NS_LOG_ERROR("eglMakeCurrent failed: 0x%04X", eglGetError());
        return false;
    }

    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    rendererName_ = std::string(renderer ? renderer : "?") + " / " + (version ? version : "?");
    GLint maxTextureSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    NS_LOG_INFO("GL ready: %s, max texture %d, surface %dx%d", rendererName_.c_str(), maxTextureSize, width_,
                height_);
    gl::checkError("context info");
    return true;
}

void GLContext::shutdown()
{
    if (!impl_) {
        return;
    }
    Impl& s = *impl_;
    if (s.display != EGL_NO_DISPLAY) {
        eglMakeCurrent(s.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (s.surface != EGL_NO_SURFACE) {
            eglDestroySurface(s.display, s.surface);
            s.surface = EGL_NO_SURFACE;
        }
        if (s.context != EGL_NO_CONTEXT) {
            eglDestroyContext(s.display, s.context);
            s.context = EGL_NO_CONTEXT;
        }
        // eglTerminate is skipped on purpose (it breaks a Piglet re-init), and
        // the module stays loaded: the compositor may still reference it.
        s.display = EGL_NO_DISPLAY;
    }
}

bool GLContext::swapBuffers()
{
    if (!eglSwapBuffers(impl_->display, impl_->surface)) {
        const EGLint error = eglGetError();
        if (error != impl_->lastSwapError) NS_LOG_ERROR("eglSwapBuffers failed: 0x%04X", error);
        impl_->lastSwapError = error;
        return false;
    }
    impl_->lastSwapError = EGL_SUCCESS;
    return true;
}

} // namespace ns
