#version 440
// Transition preview: blends the outgoing clip (`from`) into the incoming
// one (`to`) as `progress` runs 0 -> 1. Each mode redraws one of the
// transitions in src/transitions.cpp; ffmpeg's xfade draws the export.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float progress;  // 0 = all `from`, 1 = all `to`
    float mode;      // Transitions::Kind::previewMode
    float aspect;    // width / height, so circles stay round
};
layout(binding = 1) uniform sampler2D from;
layout(binding = 2) uniform sampler2D to;

const float PI = 3.14159265;

vec4 A(vec2 uv) { return texture(from, uv); }
vec4 B(vec2 uv) { return texture(to, uv); }
bool inside(vec2 uv) { return uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0; }
float luma(vec4 c) { return dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)); }

// Half the time fades out of one color, half fades into the next.
vec4 through(vec2 uv, vec4 middle) {
    return progress < 0.5 ? mix(A(uv), middle, progress * 2.0) : mix(middle, B(uv), progress * 2.0 - 1.0);
}

vec4 blurred(sampler2D tex, vec2 uv, float radius) {
    vec4 sum = vec4(0.0);
    for (int i = -4; i <= 4; ++i)
        sum += texture(tex, uv + vec2(float(i) * radius / 4.0, 0.0));
    return sum / 9.0;
}

void main() {
    vec2 uv = qt_TexCoord0;
    float p = clamp(progress, 0.0, 1.0);
    int m = int(mode + 0.5);
    vec4 c;
    if (m == 0) {            // dissolve
        c = mix(A(uv), B(uv), p);
    } else if (m == 1) {     // dip to black
        c = through(uv, vec4(0.0, 0.0, 0.0, 1.0));
    } else if (m == 2) {     // flash
        c = through(uv, vec4(1.0));
    } else if (m == 3) {     // fade through grey
        c = p < 0.5 ? mix(A(uv), vec4(vec3(luma(A(uv))), 1.0), p * 2.0)
                    : mix(vec4(vec3(luma(B(uv))), 1.0), B(uv), p * 2.0 - 1.0);
    } else if (m == 4) {     // wipe left: the edge moves right to left
        c = uv.x > 1.0 - p ? B(uv) : A(uv);
    } else if (m == 5) {     // wipe up: the edge rises from the bottom
        c = uv.y > 1.0 - p ? B(uv) : A(uv);
    } else if (m == 6) {     // slide left: both pictures move together
        c = uv.x < 1.0 - p ? A(uv + vec2(p, 0.0)) : B(uv - vec2(1.0 - p, 0.0));
    } else if (m == 7) {     // slide up
        c = uv.y < 1.0 - p ? A(uv + vec2(0.0, p)) : B(uv - vec2(0.0, 1.0 - p));
    } else if (m == 8) {     // cover: the new picture slides over a still old one
        c = uv.x > 1.0 - p ? B(uv - vec2(1.0 - p, 0.0)) : A(uv);
    } else if (m == 9) {     // iris: a circle opens from the centre
        vec2 d = (uv - 0.5) * vec2(aspect, 1.0);
        float reach = length(vec2(aspect, 1.0) * 0.5);
        c = length(d) < p * reach ? B(uv) : A(uv);
    } else if (m == 10) {    // clock: a hand sweeps around from 12
        float angle = atan(uv.x - 0.5, 0.5 - uv.y) / (2.0 * PI) + 0.5;
        c = angle < p ? B(uv) : A(uv);
    } else if (m == 11) {    // zoom into the old picture, then fade
        vec4 zoomed = A(0.5 + (uv - 0.5) / (1.0 + p * 1.5));
        c = mix(zoomed, B(uv), smoothstep(0.4, 1.0, p));
    } else if (m == 12) {    // pixelate: blocks grow, then shrink on the new picture
        float blocks = mix(160.0, 10.0, sin(p * PI));
        vec2 grid = vec2(blocks * aspect, blocks);
        vec2 q = (floor(uv * grid) + 0.5) / grid;
        c = mix(A(q), B(q), step(0.5, p));
    } else if (m == 13) {    // blur across the change
        float r = 0.04 * sin(p * PI);
        c = mix(blurred(from, uv, r), blurred(to, uv, r), p);
    } else {                 // squeeze: old narrows to a line, new widens from it
        float s = p < 0.5 ? 1.0 - p * 2.0 : p * 2.0 - 1.0;
        vec2 q = vec2(0.5 + (uv.x - 0.5) / max(s, 0.001), uv.y);
        c = inside(q) ? (p < 0.5 ? A(q) : B(q)) : vec4(0.0, 0.0, 0.0, 1.0);
    }
    fragColor = c * qt_Opacity;
}
