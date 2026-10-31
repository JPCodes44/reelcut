#version 440
// Fisheye text, live: `source` is the text block with room around it
// (FisheyeText::Padding). The lens ellipse fills this item; a point at
// radius r shows the text from radius r * (1 - k (1 - r^2)), exactly as
// FisheyeText::render() draws it for the export. A soft shadow goes under.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float k;          // MaxBulge * bulge
    vec2 shadow;      // shadow offset, in this item's 0..1 coordinates
    vec2 blur;        // shadow softness, likewise
    vec4 shadowColor; // straight RGBA; alpha 0 for no shadow
};
layout(binding = 1) uniform sampler2D source;

vec4 lens(vec2 uv) {
    vec2 d = uv * 2.0 - 1.0;
    float r2 = dot(d, d);
    float s = r2 < 1.0 ? 1.0 - k * (1.0 - r2) : 1.0;
    vec2 q = (d * s + 1.0) / 2.0;
    if (q.x < 0.0 || q.y < 0.0 || q.x > 1.0 || q.y > 1.0)
        return vec4(0.0);
    return texture(source, q);
}

void main() {
    vec4 text = lens(qt_TexCoord0);
    float a = 0.0;
    for (int i = 0; i < 12; ++i) {
        float t = float(i) * 0.5236;
        for (int j = 1; j <= 2; ++j)
            a += lens(qt_TexCoord0 - shadow + vec2(cos(t), sin(t)) * blur * float(j) / 2.0).a;
    }
    a = a / 24.0 * shadowColor.a;
    // premultiplied: text over its shadow
    vec4 under = vec4(shadowColor.rgb * a, a);
    fragColor = (text + under * (1.0 - text.a)) * qt_Opacity;
}
