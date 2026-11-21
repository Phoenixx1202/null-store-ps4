// OpenGL ES 2.0 (Piglet) implementation of the render backend.
//
// Retail PS4 firmware has no runtime GLSL compiler (libSceShaccVSH.sprx is a
// devkit-only module), so the program is linked from precompiled Piglet shader
// binaries loaded with glShaderBinary (format 0). The blobs are the ones of
// the PacBrew SDL2 port (third_party/piglet_shaders, zlib licence):
//   vertex   = position / colour / uv, one projection matrix
//   fragment = texture * vertex colour, sampling the texture as ARGB
// A whole frame goes into one vertex buffer and is drawn with one
// glDrawElements per (texture, clip) run.
#include "graphics/RenderBackend.h"

#include <cstddef>
#include <cstring>

#include "core/Log.h"
#include "graphics/GL.h"
#include "graphics/GLContext.h"

extern "C" {
#include "piglet_shaders/ps4_shader_fragment_texture_argb.h"
#include "piglet_shaders/ps4_shader_vertex_default.h"
}

namespace ns {

namespace gl {

bool checkError(const char* where)
{
    bool any = false;
    for (int guard = 0; guard < 16; ++guard) {
        const GLenum error = glGetError();
        if (error == GL_NO_ERROR) {
            break;
        }
        NS_LOG_ERROR("GL error 0x%04X at %s", error, where);
        any = true;
    }
    return any;
}

} // namespace gl

namespace {

constexpr int kVertexBuffers = 3; // rotated so a buffer in flight is never rewritten

GLuint loadShader(GLenum type, const void* blob, size_t length, const char* label)
{
    const GLuint shader = glCreateShader(type);
    if (!shader) {
        NS_LOG_ERROR("glCreateShader(%s) failed: 0x%04X", label, glGetError());
        return 0;
    }
    glShaderBinary(1, &shader, 0, blob, static_cast<GLsizei>(length));
    const GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        NS_LOG_ERROR("glShaderBinary(%s, %zu bytes) failed: 0x%04X", label, length, error);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// Column-major orthographic projection with the origin at the top-left.
void orthoTopLeft(float width, float height, float* m)
{
    memset(m, 0, sizeof(float) * 16);
    m[0] = 2.0f / width;
    m[5] = -2.0f / height;
    m[10] = -1.0f;
    m[12] = -1.0f;
    m[13] = 1.0f;
    m[15] = 1.0f;
}

class GLBackend final : public RenderBackend {
public:
    ~GLBackend() override { shutdown(); }

    const char* name() const override { return "piglet-gles2"; }

    bool init(int width, int height) override
    {
        width_ = width;
        height_ = height;
        if (!context_.init(width, height)) {
            return false;
        }
        if (!createProgram() || !createBuffers()) {
            return false;
        }
        orthoTopLeft(static_cast<float>(width), static_cast<float>(height), projection_);

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_SCISSOR_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // premultiplied alpha
        glViewport(0, 0, width_, height_);
        gl::checkError("renderer state");
        ready_ = true;
        NS_LOG_INFO("Render backend ready (%s)", context_.rendererName().c_str());
        return true;
    }

    void shutdown() override
    {
        if (!context_.valid()) {
            return;
        }
        if (vertexBuffers_[0]) {
            glDeleteBuffers(kVertexBuffers, vertexBuffers_);
            memset(vertexBuffers_, 0, sizeof(vertexBuffers_));
        }
        if (indexBuffer_) {
            glDeleteBuffers(1, &indexBuffer_);
            indexBuffer_ = 0;
        }
        if (program_) {
            glDeleteProgram(program_);
            program_ = 0;
        }
        context_.shutdown();
        ready_ = false;
    }

    TextureId createTexture(int width, int height, const uint8_t* rgba, bool linearFilter) override
    {
        if (width <= 0 || height <= 0 || !rgba) {
            return 0;
        }
        GLuint id = 0;
        glGenTextures(1, &id);
        if (!id) {
            NS_LOG_ERROR("glGenTextures failed");
            return 0;
        }
        glBindTexture(GL_TEXTURE_2D, id);
        boundTexture_ = id;
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     shaderOrder(rgba, width, height));
        const GLint filter = linearFilter ? GL_LINEAR : GL_NEAREST;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        const GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            NS_LOG_ERROR("Texture upload failed (%dx%d): GL error 0x%04X", width, height, error);
            glDeleteTextures(1, &id);
            boundTexture_ = 0;
            return 0;
        }
        return id;
    }

    bool updateTexture(TextureId texture, int width, int height, const uint8_t* rgba) override
    {
        if (!texture || !rgba) {
            return false;
        }
        glBindTexture(GL_TEXTURE_2D, texture);
        boundTexture_ = texture;
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
                        shaderOrder(rgba, width, height));
        return !gl::checkError("texture update");
    }

    void destroyTexture(TextureId texture) override
    {
        if (texture && context_.valid()) {
            const GLuint id = texture;
            glDeleteTextures(1, &id);
            if (boundTexture_ == id) {
                boundTexture_ = 0;
            }
        }
    }

    void beginFrame(const Color& clearColor) override
    {
        ++frame_;
        glViewport(0, 0, width_, height_);
        glDisable(GL_SCISSOR_TEST);
        appliedClip_ = ClipRect();
        glClearColor(clearColor.r, clearColor.g, clearColor.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(program_);
        glUniformMatrix4fv(uProjection_, 1, GL_FALSE, projection_);
        glActiveTexture(GL_TEXTURE0);
        if (uTexture_ >= 0) {
            glUniform1i(uTexture_, 0);
        }
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer_);
        glEnableVertexAttribArray(static_cast<GLuint>(aPosition_));
        glEnableVertexAttribArray(static_cast<GLuint>(aColor_));
        glEnableVertexAttribArray(static_cast<GLuint>(aTexCoord_));
        boundTexture_ = 0;
        if (frame_ <= 3) {
            gl::checkError("beginFrame");
        }
    }

    void draw(const Vertex* vertices, size_t vertexCount, const DrawCommand* commands,
              size_t commandCount) override
    {
        if (!vertexCount || !commandCount) {
            return;
        }
        const GLuint buffer = vertexBuffers_[bufferCursor_];
        bufferCursor_ = (bufferCursor_ + 1) % kVertexBuffers;
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertexCount * sizeof(Vertex)), vertices,
                     GL_STREAM_DRAW);
        glVertexAttribPointer(static_cast<GLuint>(aPosition_), 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<const void*>(offsetof(Vertex, x)));
        glVertexAttribPointer(static_cast<GLuint>(aTexCoord_), 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<const void*>(offsetof(Vertex, u)));
        glVertexAttribPointer(static_cast<GLuint>(aColor_), 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex),
                              reinterpret_cast<const void*>(offsetof(Vertex, r)));

        for (size_t i = 0; i < commandCount; ++i) {
            const DrawCommand& command = commands[i];
            if (command.clip != appliedClip_) {
                applyClip(command.clip);
            }
            if (command.texture != boundTexture_) {
                glBindTexture(GL_TEXTURE_2D, command.texture);
                boundTexture_ = command.texture;
            }
            const uintptr_t offset = static_cast<uintptr_t>(command.firstQuad) * 6u * sizeof(GLushort);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(command.quadCount * 6), GL_UNSIGNED_SHORT,
                           reinterpret_cast<const void*>(offset));
        }
        if (frame_ <= 3) {
            gl::checkError("draw");
        }
    }

    bool present() override
    {
        if (appliedClip_.enabled) {
            applyClip(ClipRect());
        }
        return context_.swapBuffers();
    }

    bool capture(std::vector<uint8_t>& rgba) override
    {
        const size_t rowBytes = static_cast<size_t>(width_) * 4;
        std::vector<uint8_t> raw(rowBytes * static_cast<size_t>(height_));
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());
        if (gl::checkError("glReadPixels")) {
            return false;
        }
        rgba.resize(raw.size());
        for (int y = 0; y < height_; ++y) { // GL rows run bottom to top
            memcpy(&rgba[static_cast<size_t>(y) * rowBytes], &raw[static_cast<size_t>(height_ - 1 - y) * rowBytes],
                   rowBytes);
        }
        return true;
    }

private:
    bool createProgram()
    {
        const GLuint vertex = loadShader(GL_VERTEX_SHADER, PS4_SHADER_VERTEX_DEFAULT,
                                         static_cast<size_t>(PS4_SHADER_VERTEX_DEFAULT_LENGTH), "vertex");
        const GLuint fragment = loadShader(GL_FRAGMENT_SHADER, PS4_SHADER_FRAGMENT_TEXTURE_ARGB,
                                           static_cast<size_t>(PS4_SHADER_FRAGMENT_TEXTURE_ARGB_LENGTH), "fragment");
        if (!vertex || !fragment) {
            if (vertex) glDeleteShader(vertex);
            if (fragment) glDeleteShader(fragment);
            return false;
        }
        program_ = glCreateProgram();
        glAttachShader(program_, vertex);
        glAttachShader(program_, fragment);
        glLinkProgram(program_);
        GLint linked = 0;
        glGetProgramiv(program_, GL_LINK_STATUS, &linked);
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        if (!linked) {
            char info[512] = { 0 };
            glGetProgramInfoLog(program_, sizeof(info) - 1, nullptr, info);
            NS_LOG_ERROR("glLinkProgram failed: %s", info);
            glDeleteProgram(program_);
            program_ = 0;
            return false;
        }
        glGetError();

        aPosition_ = glGetAttribLocation(program_, "a_position");
        aColor_ = glGetAttribLocation(program_, "a_color");
        aTexCoord_ = glGetAttribLocation(program_, "a_texCoord");
        uProjection_ = glGetUniformLocation(program_, "u_projection");
        uTexture_ = glGetUniformLocation(program_, "u_texture");
        if (aPosition_ < 0 || aColor_ < 0 || aTexCoord_ < 0 || uProjection_ < 0) {
            NS_LOG_ERROR("Shader program is missing inputs (pos=%d color=%d uv=%d proj=%d)", aPosition_, aColor_,
                         aTexCoord_, uProjection_);
            return false;
        }
        return true;
    }

    bool createBuffers()
    {
        glGenBuffers(kVertexBuffers, vertexBuffers_);
        glGenBuffers(1, &indexBuffer_);
        // Two triangles per quad: vertices are top-left, top-right,
        // bottom-left, bottom-right.
        std::vector<GLushort> indices(kMaxQuads * 6);
        for (size_t quad = 0; quad < kMaxQuads; ++quad) {
            const GLushort base = static_cast<GLushort>(quad * 4);
            indices[quad * 6 + 0] = base;
            indices[quad * 6 + 1] = static_cast<GLushort>(base + 1);
            indices[quad * 6 + 2] = static_cast<GLushort>(base + 2);
            indices[quad * 6 + 3] = static_cast<GLushort>(base + 1);
            indices[quad * 6 + 4] = static_cast<GLushort>(base + 3);
            indices[quad * 6 + 5] = static_cast<GLushort>(base + 2);
        }
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(GLushort)),
                     indices.data(), GL_STATIC_DRAW);
        for (GLuint buffer : vertexBuffers_) {
            glBindBuffer(GL_ARRAY_BUFFER, buffer);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(kMaxQuads * 4 * sizeof(Vertex)), nullptr,
                         GL_STREAM_DRAW);
        }
        return !gl::checkError("buffers");
    }

    void applyClip(const ClipRect& clip)
    {
        if (!clip.enabled) {
            glDisable(GL_SCISSOR_TEST);
        } else {
            glEnable(GL_SCISSOR_TEST);
            glScissor(clip.x, height_ - (clip.y + clip.h), clip.w, clip.h); // GL's origin is bottom-left
        }
        appliedClip_ = clip;
    }

    // The fragment program swaps red and blue when it samples, so textures are
    // stored as BGRA.
    const uint8_t* shaderOrder(const uint8_t* rgba, int width, int height)
    {
        const size_t bytes = static_cast<size_t>(width) * height * 4;
        scratch_.resize(bytes);
        for (size_t i = 0; i < bytes; i += 4) {
            scratch_[i] = rgba[i + 2];
            scratch_[i + 1] = rgba[i + 1];
            scratch_[i + 2] = rgba[i];
            scratch_[i + 3] = rgba[i + 3];
        }
        return scratch_.data();
    }

    GLContext context_;
    GLuint program_ = 0;
    GLuint vertexBuffers_[kVertexBuffers] = { 0, 0, 0 };
    GLuint indexBuffer_ = 0;
    int bufferCursor_ = 0;
    GLint aPosition_ = -1;
    GLint aColor_ = -1;
    GLint aTexCoord_ = -1;
    GLint uProjection_ = -1;
    GLint uTexture_ = -1;
    GLuint boundTexture_ = 0;
    ClipRect appliedClip_;
    float projection_[16] = { 0 };
    std::vector<uint8_t> scratch_;
    int width_ = 0;
    int height_ = 0;
    uint64_t frame_ = 0;
    bool ready_ = false;
};

} // namespace

std::unique_ptr<RenderBackend> createRenderBackend()
{
    return std::make_unique<GLBackend>();
}

} // namespace ns
