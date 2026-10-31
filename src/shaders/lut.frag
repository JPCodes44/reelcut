#version 440
// Filter preview: a 3D color lookup table stored as a 2D "strip" image.
// The 512x512 image holds 64 blue levels as an 8x8 grid of tiles; inside
// each 64x64 tile red runs left to right and green top to bottom.
// We look up the two tiles either side of the pixel's blue level and blend
// between them, so the result is smooth along all three axes.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float amount;   // 0 = untouched, 1 = full look
};
layout(binding = 1) uniform sampler2D source;
layout(binding = 2) uniform sampler2D lut;

vec2 tileOrigin(float level) {
    return vec2(mod(level, 8.0), floor(level / 8.0)) / 8.0;
}

void main() {
    vec4 texel = texture(source, qt_TexCoord0);
    // Layers are premultiplied; grade the straight color.
    vec3 rgb = texel.a > 0.0 ? texel.rgb / texel.a : texel.rgb;
    float blue = clamp(rgb.b, 0.0, 1.0) * 63.0;
    // Step to the centre of a cell: 63 steps across a 64-pixel tile.
    vec2 inTile = clamp(rgb.rg, 0.0, 1.0) * (63.0 / 512.0) + 0.5 / 512.0;
    vec3 low = texture(lut, tileOrigin(floor(blue)) + inTile).rgb;
    vec3 high = texture(lut, tileOrigin(ceil(blue)) + inTile).rgb;
    vec3 graded = mix(low, high, fract(blue));
    fragColor = vec4(mix(rgb, graded, amount) * texel.a, texel.a) * qt_Opacity;
}
