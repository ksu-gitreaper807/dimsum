/*
    SPDX-FileCopyrightText: 2026 The dimsum contributors
    SPDX-License-Identifier: MIT

    OpenGL ES variant of the Software Dim shader.

    Byte-for-byte the same program as software_dim_core.frag — `texture()`,
    `in`/`out` varyings and `fragColor` are all valid GLSL ES 3.x, and KWin
    generates the `#version 300 es` line plus the precision qualifiers itself.

    See software_dim_core.frag for the note about KWin's injected header.
*/

uniform float dimAmount;

void main()
{
    vec4 color = texture(sampler, texcoord0);
    color.rgb *= dimAmount;
    fragColor = color;
}
