#version 440
// Color wheels, live: the same steps as ColorGrade::apply() in
// src/colorgrade.cpp, which bakes them into a .cube for the export.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec3 wb;      // white balance gains (temperature, tint)
    vec3 off;     // offset
    vec3 lift;
    vec3 gain;
    vec3 gexp;    // gamma, as an exponent
    float contrast;
    float pivot;
    float saturation;
    float hue;    // radians
};
layout(binding = 1) uniform sampler2D source;

void main() {
    vec4 texel = texture(source, qt_TexCoord0);
    vec3 v = texel.a > 0.0 ? texel.rgb / texel.a : texel.rgb;
    v = v * wb + off;
    v = lift + v * (1.0 - lift);
    v *= gain;
    v = pow(max(v, vec3(0.0)), gexp);
    v = (v - pivot) * contrast + pivot;
    if (hue != 0.0) {
        float k = 0.57735027;
        float d = (v.r + v.g + v.b) * k;
        vec3 cr = k * vec3(v.b - v.g, v.r - v.b, v.g - v.r);
        v = v * cos(hue) + cr * sin(hue) + k * d * (1.0 - cos(hue));
    }
    float y = dot(v, vec3(0.2126, 0.7152, 0.0722));
    v = clamp(y + (v - y) * saturation, 0.0, 1.0);
    fragColor = vec4(v * texel.a, texel.a) * qt_Opacity;
}
