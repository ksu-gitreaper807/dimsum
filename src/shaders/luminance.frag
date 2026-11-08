// SPDX-License-Identifier: MIT
uniform sampler2D sampler;
uniform int transferFunction; // KWin: 0 = sRGB, 3 = gamma 2.2
uniform vec3 luminanceWeights;
in vec2 texcoord0;
out vec4 fragColor;

vec3 decode(vec3 encoded)
{
    encoded = clamp(encoded, 0.0, 1.0);
    if (transferFunction == 3) {
        return pow(encoded, vec3(2.2));
    }
    return mix(encoded / 12.92, pow((encoded + 0.055) / 1.055, vec3(2.4)),
               step(vec3(0.04045), encoded));
}

void main()
{
    // Four stratified taps per cell, all decoded before averaging. 4096 taps
    // total, independent of the native output resolution. No stored snapshot.
    vec2 delta = vec2(0.25 / 32.0);
    vec3 mean = decode(texture(sampler, texcoord0 + vec2(-delta.x, -delta.y)).rgb);
    mean += decode(texture(sampler, texcoord0 + vec2(delta.x, -delta.y)).rgb);
    mean += decode(texture(sampler, texcoord0 + vec2(-delta.x, delta.y)).rgb);
    mean += decode(texture(sampler, texcoord0 + vec2(delta.x, delta.y)).rgb);
    float luminance = clamp(dot(mean * 0.25, luminanceWeights), 0.0, 1.0);
    fragColor = vec4(vec3(luminance), 1.0);
}
