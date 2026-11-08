// SPDX-License-Identifier: MIT
// Exercises the actual KWin texture, shader, mip reduction, fence and readback
// path in an isolated software-rendered context. Never touches the desktop.
#include "luminancesampler.h"
#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <opengl/eglcontext.h>
#include <opengl/egldisplay.h>
#include <opengl/glframebuffer.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <opengl/gltexture.h>
#include <thread>

static void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    auto display = KWin::EglDisplay::create(eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr), nullptr);
    if (!display) {
        std::cerr << "SKIP: no surfaceless EGL display\n";
        return 77;
    }
    auto context = KWin::EglContext::create(display.get(), EGL_NO_CONFIG_KHR, nullptr);
    if (!context || epoxy_gl_version() < 30) {
        std::cerr << "SKIP: no GLES 3 context\n";
        return 77;
    }
    std::cout << "Test renderer: " << glGetString(GL_RENDERER) << '\n';
    auto shader = KWin::ShaderManager::instance()->generateShaderFromFile(
        KWin::ShaderTrait::MapTexture, QString(), QStringLiteral(":/softwaredim/shaders/luminance.frag"));
    check(bool(shader), "Production luminance shader compiles");
    auto parentTexture = KWin::GLTexture::allocate(GL_RGBA8, QSize(64, 64));
    KWin::GLFramebuffer parent(parentTexture.get());
    KWin::GLFramebuffer::pushFramebuffer(&parent);
    KWin::LuminanceSampler sampler;
    check(sampler.valid(), "Production sampler initializes");
    check(!sampler.takeReady(), "No sample before submission");
    const QVector3D weights(0.2126, 0.7152, 0.0722);
    const auto sample = [&](const QImage& image, int tf, QVector3D customWeights = QVector3D()) {
        auto source = KWin::GLTexture::upload(image);
        check(bool(source), "Fixture upload succeeds");
        source->setFilter(GL_LINEAR);
        source->setWrapMode(GL_CLAMP_TO_EDGE);
        // Deliberately dirty pack state: the production path must restore it.
        glPixelStorei(GL_PACK_ALIGNMENT, 8);
        glPixelStorei(GL_PACK_ROW_LENGTH, 19);
        glPixelStorei(GL_PACK_SKIP_ROWS, 2);
        glPixelStorei(GL_PACK_SKIP_PIXELS, 3);
        GLuint previousBuffer;
        glGenBuffers(1, &previousBuffer);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, previousBuffer);
        sampler.submit(*source, *shader, tf, customWeights.isNull() ? weights : customWeights);
        check(sampler.pending(), "Readback has a fence");
        sampler.submit(*source, *shader, tf, weights); // Must not overwrite in-flight result.
        GLint state;
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &state);
        check(GLuint(state) == previousBuffer, "Pack buffer restored");
        glGetIntegerv(GL_PACK_ALIGNMENT, &state);
        check(state == 8, "Pack alignment restored");
        glGetIntegerv(GL_PACK_ROW_LENGTH, &state);
        check(state == 19, "Pack row length restored");
        glGetIntegerv(GL_PACK_SKIP_ROWS, &state);
        check(state == 2, "Pack skip rows restored");
        glGetIntegerv(GL_PACK_SKIP_PIXELS, &state);
        check(state == 3, "Pack skip pixels restored");
        check(KWin::GLFramebuffer::currentFramebuffer() == &parent, "Framebuffer stack restored");
        GLint viewport[4];
        glGetIntegerv(GL_VIEWPORT, viewport);
        check(viewport[2] == 64 && viewport[3] == 64, "Viewport restored");
        std::optional<double> mean;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!(mean = sampler.takeReady()) && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(mean.has_value(), "Async sample eventually completes");
        check(!sampler.pending(), "Fence consumed");
        check(glGetError() == GL_NO_ERROR, "No GL errors");
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glDeleteBuffers(1, &previousBuffer);
        return mean.value();
    };
    QImage image(128, 128, QImage::Format_RGBA8888_Premultiplied);
    for (const auto& [color, expected] : { std::pair { QColor(Qt::black), 0.0 }, { QColor(Qt::white), 1.0 }, { QColor(Qt::red), 0.2126 },
             { QColor(Qt::green), 0.7152 }, { QColor(Qt::blue), 0.0722 }, { QColor(128, 128, 128), std::pow(128.0 / 255, 2.2) } }) {
        image.fill(color);
        const double actual = sample(image, 3);
        std::cout << "Mean " << actual << " expected " << expected << '\n';
        check(std::abs(actual - expected) < 0.012, "Gamma-2.2 fixture luminance correct");
    }
    image.fill(QColor(128, 128, 128));
    check(std::abs(sample(image, 0) - 0.21586) < 0.012, "sRGB decoding correct");
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            image.setPixelColor(x, y, x < 64 ? Qt::white : Qt::black);
        }
    }
    check(std::abs(sample(image, 3) - 0.5) < 0.012, "Half white/black averages linear light, not encoded values");
    image.fill(Qt::red);
    check(std::abs(sample(image, 3, QVector3D(0.4, 0.5, 0.1)) - 0.4) < 0.012, "Output colour primaries respected");
    KWin::GLFramebuffer::popFramebuffer();
    std::cout << "Luminance pipeline checks passed\n";
}
