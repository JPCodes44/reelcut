#version 440
// Body effect preview: `mask` is the person mask video (white = person),
// playing in step with the clip. Each mode changes the background (or the
// mask's edge) and keeps the person, like maskedmerge does on export.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float mode;    // Body::Kind::previewMode
    float amount;  // 0..1
    float aspect;  // width / height
};
layout(binding = 1) uniform sampler2D source;
layout(binding = 2) uniform sampler2D mask;

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
float person(vec2 uv) { return texture(mask, uv).r; }

// Gaussian blur of `sigma` pixels (of a 1080-wide frame), matching ffmpeg's
// gblur on export. 64 taps on a golden-angle spiral, weighted by the
// Gaussian, cover the disk evenly; each pixel turns the spiral by its own
// angle, so the gaps between taps turn into fine noise instead of the
// stepped, ghosted copies a fixed grid of taps leaves.
vec4 gaussian(vec2 uv, float sigma) {
    float radius = 2.5 * sigma;
    float turn = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    vec4 sum = vec4(0.0);
    float total = 0.0;
    for (int k = 0; k < 64; ++k) {
        float d = radius * sqrt((float(k) + 0.5) / 64.0);
        float a = float(k) * 2.3999632 + turn;
        float w = exp(-0.5 * (d / sigma) * (d / sigma));
        sum += texture(source, clamp(uv + vec2(cos(a), sin(a) * aspect) * d / 1080.0, 0.0, 1.0)) * w;
        total += w;
    }
    return sum / total;
}

void main() {
    vec2 uv = qt_TexCoord0;
    vec4 picture = texture(source, uv);
    float m = person(uv);
    int k = int(mode + 0.5);
    vec3 background = picture.rgb;
    if (k == 0) {          // background blur: the same sigma as the export's gblur
        background = gaussian(uv, 4.0 + 36.0 * amount).rgb;
    } else if (k == 1) {   // color pop: the background goes grey
        background = mix(picture.rgb, vec3(luma(picture.rgb)), amount);
    } else if (k == 2) {   // spotlight: the background darkens and dulls
        vec3 dull = mix(vec3(luma(picture.rgb)), picture.rgb, 1.0 - 0.5 * amount);
        background = dull * (1.0 - 0.6 * amount);
    } else if (k == 4) {   // backdrop: a deep blue-grey wall
        background = mix(picture.rgb, vec3(0.094, 0.102, 0.157), amount);
    }

    vec3 color = mix(background, picture.rgb, m);
    if (k == 3) {          // neon outline: where the mask changes fastest
        vec2 px = vec2(3.0 / 270.0, 3.0 / 480.0);
        float edge = abs(person(uv + vec2(px.x, 0.0)) - person(uv - vec2(px.x, 0.0)))
                   + abs(person(uv + vec2(0.0, px.y)) - person(uv - vec2(0.0, px.y)));
        float glow = clamp(edge * (2.0 + 6.0 * amount), 0.0, 1.0);
        color = mix(picture.rgb, vec3(0.0, 0.92, 1.0), glow);
    }
    fragColor = vec4(color * picture.a, picture.a) * qt_Opacity;
}
