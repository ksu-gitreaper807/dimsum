/*
    SPDX-FileCopyrightText: 2026 The dimsum contributors
    SPDX-License-Identifier: MIT

    Desktop-OpenGL twin of software_dim.frag — byte-for-byte the same program.

    KWin 6.7.5's ShaderManager::generateShaderFromFile() loads the exact path
    it is given (software_dim.frag) and does NOT resolve a "_core" variant,
    even though the header comment in src/opengl/glshadermanager.h documents
    that behaviour. So on 6.7.5 this file is never read; it stays in the .qrc
    so the pair keeps working unchanged if a future loader honours the
    documented "_core" suffix again.

    Like its twin, it declares `sampler` / `texcoord0` / `fragColor` itself:
    KWin injects only `#version`, precision qualifiers and TRAIT_* defines
    (GLShader::preprocess in src/opengl/glshader.cpp). Do not remove those
    declarations — without them the shader fails with "undeclared identifier".

    Colour space: the multiplication happens on the values KWin writes into the
    output framebuffer, i.e. gamma-encoded (sRGB transfer function) for an SDR
    output. This is intentional: perceptual dimming. See README.md →
    "Colour space, gamma and HDR".
*/

uniform sampler2D sampler;
uniform float dimAmount;

in vec2 texcoord0;

out vec4 fragColor;

void main()
{
    vec4 color = texture(sampler, texcoord0);
    color.rgb *= dimAmount;
    fragColor = color;
}
