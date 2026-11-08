// SPDX-License-Identifier: MIT
#pragma once

#include <QVector3D>
#include <epoxy/gl.h>
#include <memory>
#include <optional>

namespace KWin
{
class GLTexture;
class GLFramebuffer;
class GLShader;

// Called only with KWin's current GL context. All image work stays on that
// renderer; no second context/device, screenshots, files, or full-size readback.
class LuminanceSampler
{
public:
    LuminanceSampler();
    ~LuminanceSampler();
    bool valid() const;
    bool pending() const { return m_fence != nullptr; }
    bool failed() const { return m_failed; }
    void submit(GLTexture& scene, GLShader& shader, int transferFunction, const QVector3D& weights);
    std::optional<double> takeReady();

private:
    std::unique_ptr<GLTexture> m_texture;
    std::unique_ptr<GLFramebuffer> m_framebuffer;
    std::unique_ptr<GLFramebuffer> m_averageFramebuffer;
    GLuint m_averageHandle = 0;
    GLuint m_buffer = 0;
    GLsync m_fence = nullptr;
    bool m_failed = false;
};
}
