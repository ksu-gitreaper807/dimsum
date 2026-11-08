// SPDX-License-Identifier: MIT
#include "luminancesampler.h"

#include <QMatrix4x4>
#include <opengl/eglcontext.h>
#include <opengl/glframebuffer.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <opengl/gltexture.h>

namespace KWin
{
namespace
{
constexpr int sampleSize = 32;
constexpr int averageLevel = 5;
}

LuminanceSampler::LuminanceSampler()
{
    const auto context = EglContext::currentContext();
    if (!context || epoxy_gl_version() < 30 || !context->haveSyncFences() || !context->hasMapBufferRange()) {
        m_failed = true;
        return;
    }
    m_texture = GLTexture::allocate(GL_RGBA8, QSize(sampleSize, sampleSize), averageLevel + 1);
    if (!m_texture) {
        return;
    }
    m_texture->setFilter(GL_LINEAR);
    m_texture->setWrapMode(GL_CLAMP_TO_EDGE);
    // KWin's GLES allocation may initially define only level zero. Define
    // the entire mip chain before attaching its last level to the read FBO.
    m_texture->bind();
    m_texture->generateMipmaps();
    m_texture->unbind();
    m_framebuffer = std::make_unique<GLFramebuffer>(m_texture.get());
    if (!m_framebuffer->valid()) {
        return;
    }
    glGenFramebuffers(1, &m_averageHandle);
    m_averageFramebuffer = std::make_unique<GLFramebuffer>(m_averageHandle, QSize(1, 1));
    GLFramebuffer::pushFramebuffer(m_averageFramebuffer.get());
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture->texture(), averageLevel);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    GLFramebuffer::popFramebuffer();
    if (!complete) {
        m_failed = true;
        return;
    }
    GLint previous = 0;
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &previous);
    glGenBuffers(1, &m_buffer);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, m_buffer);
    glBufferData(GL_PIXEL_PACK_BUFFER, 4, nullptr, GL_STREAM_READ);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, previous);
}

LuminanceSampler::~LuminanceSampler()
{
    if (m_fence) {
        glDeleteSync(m_fence);
    }
    if (m_buffer) {
        glDeleteBuffers(1, &m_buffer);
    }
    m_averageFramebuffer.reset();
    if (m_averageHandle) {
        glDeleteFramebuffers(1, &m_averageHandle);
    }
}

bool LuminanceSampler::valid() const { return !m_failed && m_buffer && m_framebuffer && m_framebuffer->valid(); }

void LuminanceSampler::submit(GLTexture& scene, GLShader& shader, int transferFunction, const QVector3D& weights)
{
    if (!valid() || pending()) {
        return;
    }
    // The caller disables blending/scissoring/sRGB conversion and restores
    // them afterwards. Decode each source tap BEFORE averaging luminance.
    GLFramebuffer::pushFramebuffer(m_framebuffer.get());
    QMatrix4x4 projection;
    projection.ortho(0, sampleSize, sampleSize, 0, -1, 1);
    ShaderManager::instance()->pushShader(&shader);
    shader.setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, projection);
    shader.setUniform("transferFunction", transferFunction);
    shader.setUniform("luminanceWeights", weights);
    scene.render(QSizeF(sampleSize, sampleSize));
    ShaderManager::instance()->popShader();
    GLFramebuffer::popFramebuffer();

    // Parallel mipmap reduction: 32x32 -> 16x16 -> ... -> one mean pixel.
    m_texture->bind();
    m_texture->generateMipmaps();
    m_texture->unbind();
    GLFramebuffer::pushFramebuffer(m_averageFramebuffer.get());
    GLint previousBuffer, alignment, rowLength, skipRows, skipPixels;
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &previousBuffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
    glGetIntegerv(GL_PACK_ROW_LENGTH, &rowLength);
    glGetIntegerv(GL_PACK_SKIP_ROWS, &skipRows);
    glGetIntegerv(GL_PACK_SKIP_PIXELS, &skipPixels);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, m_buffer);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    m_fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    m_failed = !m_fence;
    glFlush(); // Submit, never wait for GPU completion in the paint path.
    glBindBuffer(GL_PIXEL_PACK_BUFFER, previousBuffer);
    glPixelStorei(GL_PACK_ALIGNMENT, alignment);
    glPixelStorei(GL_PACK_ROW_LENGTH, rowLength);
    glPixelStorei(GL_PACK_SKIP_ROWS, skipRows);
    glPixelStorei(GL_PACK_SKIP_PIXELS, skipPixels);
    GLFramebuffer::popFramebuffer();
}

std::optional<double> LuminanceSampler::takeReady()
{
    if (!m_fence) {
        return std::nullopt;
    }
    const GLenum result = glClientWaitSync(m_fence, 0, 0);
    if (result == GL_TIMEOUT_EXPIRED) {
        return std::nullopt;
    }
    glDeleteSync(m_fence);
    m_fence = nullptr;
    if (result == GL_WAIT_FAILED) {
        m_failed = true;
        return std::nullopt;
    }
    GLint previous;
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &previous);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, m_buffer);
    const auto* pixel = static_cast<const unsigned char*>(glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, 4, GL_MAP_READ_BIT));
    std::optional<double> mean;
    if (pixel) {
        mean = pixel[0] / 255.0;
        if (!glUnmapBuffer(GL_PIXEL_PACK_BUFFER)) {
            mean.reset();
        }
    }
    m_failed = !mean.has_value();
    glBindBuffer(GL_PIXEL_PACK_BUFFER, previous);
    return mean;
}
}
