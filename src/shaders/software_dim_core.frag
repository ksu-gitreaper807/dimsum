/*
    SPDX-FileCopyrightText: 2026 The dimsum contributors
    SPDX-License-Identifier: MIT

    Desktop-OpenGL (GLSL 1.40+) variant of the Software Dim shader.
    KWin's ShaderManager picks "<name>_core.frag" automatically when the
    context reports GLSL >= 1.40, and "<name>.frag" for OpenGL ES.

    ---------------------------------------------------------------------------
    IMPORTANT — do not re-declare KWin's injected uniforms/varyings.

    KWin's ShaderManager prepends a generated header to this file containing,
    among others:

        uniform mat4 modelViewProjectionMatrix;
        in  vec2 texcoord0;
        out vec4 fragColor;
        uniform sampler2D sampler;      // from ShaderTrait::MapTexture

    Declaring any of them again here is a GLSL redefinition error and the
    shader will fail to link. That is why this file only declares its own
    uniform. If linking instead fails with "undeclared identifier 'sampler'"
    or "'texcoord0'", your ShaderManager does not inject them: add the four
    lines above (minus modelViewProjectionMatrix, which is always injected)
    back at the top of this file and rebuild. See README.md → Troubleshooting.
    ---------------------------------------------------------------------------

    Colour space: the multiplication happens on the values KWin writes into the
    output framebuffer, i.e. gamma-encoded (sRGB transfer function) for an SDR
    output. This is intentional: perceptual dimming. See README.md →
    "Colour space, gamma and HDR".
*/

uniform float dimAmount;

void main()
{
    vec4 color = texture(sampler, texcoord0);
    color.rgb *= dimAmount;
    fragColor = color;
}
