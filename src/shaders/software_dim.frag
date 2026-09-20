/*
    SPDX-FileCopyrightText: 2026 The dimsum contributors
    SPDX-License-Identifier: MIT

    Software Dim fragment shader — this is the file KWin 6.7.5 loads, verbatim,
    on every context (desktop GL and OpenGL ES alike).

    How it is compiled: ShaderManager::generateShaderFromFile() reads this file
    as-is and prepends only `#define TRAIT_*` lines; GLShader::preprocess()
    (src/opengl/glshader.cpp) then prepends `#version 140` on desktop GL or
    `#version 300 es` plus precision qualifiers on GLES. Nothing else is
    injected — in particular the shader MUST declare `sampler`, `texcoord0`
    and `fragColor` itself, exactly like KWin's own effect shaders do (see
    src/plugins/invert/shaders/invert.frag). A `#version` line here would be
    ignored, so there is none.

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
