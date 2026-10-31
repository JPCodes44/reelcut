#version 440
// Video effect preview. One branch per effect in src/effects.cpp; ffmpeg
// draws the export. `local` is seconds since the effect began.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float mode;       // Effects::Kind::previewMode, or -1 for none
    float local;      // seconds into the effect
    float intensity;  // 0..1
    float speed;      // 0..1
    float aspect;     // width / height
};
layout(binding = 1) uniform sampler2D source;

const float PI = 3.14159265;

// A cheap repeatable pseudo-random number from a 2D seed.
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
vec4 at(vec2 uv) { return texture(source, clamp(uv, 0.0, 1.0)); }

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
    int m = int(mode + 0.5);
    float i = intensity;
    vec4 c = at(uv);
    if (mode < -0.5) {
        // no effect active
    } else if (m == 0) {          // shake: move the picture on two sine waves
        float amp = (10.0 + 40.0 * i) / 1920.0;
        float f = 12.0 + 40.0 * speed;
        vec2 offset = vec2(sin(local * f) / aspect, cos(local * f * 1.37)) * amp;
        c = at(0.5 + (uv - 0.5) / 1.1 + offset);
    } else if (m == 1) {          // zoom pulse: zoom swells on a sine
        float z = 1.0 + (0.05 + 0.25 * i) * abs(sin(PI * (0.5 + 3.5 * speed) * local));
        c = at(0.5 + (uv - 0.5) / z);
    } else if (m == 2) {          // strobe: brightness spikes that fall off fast
        float phase = fract(local * (1.0 + 7.0 * speed));
        c.rgb += (0.2 + 0.6 * i) * exp(-pow(phase * 6.0, 2.0));
    } else if (m == 3) {          // VHS: color fringes, scanlines, noise
        float shift = (3.0 + 9.0 * i) / 1080.0;
        c = vec4(at(uv + vec2(shift, 0.0)).r, c.g, at(uv - vec2(shift, 0.0)).b, c.a);
        c.rgb = mix(vec3(luma(c.rgb)), c.rgb, 1.25);
        c.rgb += (hash(uv * 1000.0 + local) - 0.5) * (0.06 + 0.16 * i);
        if (mod(floor(uv.y * 1920.0), 4.0) < 1.0) c.rgb *= 1.0 - (0.15 + 0.35 * i);
    } else if (m == 4) {          // RGB split: red one way, blue the other
        float shift = (6.0 + 30.0 * i) / 1080.0;
        c = vec4(at(uv - vec2(shift, 0.0)).r, at(uv + vec2(0.0, shift / 3.0 / aspect)).g,
                 at(uv + vec2(shift, 0.0)).b, c.a);
    } else if (m == 5) {          // glitch: random sideways jumps per band and moment
        float moment = floor(local * (4.0 + 20.0 * speed));
        float band = floor(uv.y * 24.0);
        float jump = hash(vec2(band, moment)) > 0.6 ? (hash(vec2(moment, band + 7.0)) - 0.5) : 0.0;
        vec2 q = vec2(uv.x + jump * (30.0 + 120.0 * i) / 1080.0, uv.y);
        float split = (4.0 + 12.0 * i) / 1080.0;
        c = vec4(at(q + vec2(split, 0.0)).r, at(q).g, at(q - vec2(split, 0.0)).b, c.a);
    } else if (m == 6) {          // vignette: darker toward the corners
        vec2 d = (uv - 0.5) * vec2(aspect, 1.0);
        float r = length(d) / length(vec2(aspect, 1.0) * 0.5);
        c.rgb *= 1.0 - smoothstep(0.3, 1.0, r) * (0.35 + 0.6 * i);
    } else if (m == 7) {          // blur: the same sigma as the export's gblur
        c = gaussian(uv, 2.0 + 28.0 * i);
    } else if (m == 8) {          // film grain: fresh noise every frame
        c.rgb += (hash(uv * 1500.0 + fract(local * 30.0)) - 0.5) * (0.05 + 0.25 * i);
    } else if (m == 9) {          // mono flicker: black and white, on and off
        if (fract(local * (1.0 + 9.0 * speed)) < 0.5)
            c.rgb = vec3(clamp((luma(c.rgb) - 0.5) * (1.0 + 0.4 * i) + 0.5, 0.0, 1.0));
    } else if (m == 10) {         // invert, mixed by intensity
        c.rgb = mix(c.rgb, 1.0 - c.rgb, i);
    } else if (m == 11) {         // mirror: left half reflected onto the right
        c = at(vec2(uv.x < 0.5 ? uv.x : 1.0 - uv.x, uv.y));
    } else if (m == 12) {         // light trails: a soft glow stands in for the
        // export's frame-to-frame trails, which a single frame can't show
        vec3 glow = vec3(0.0);
        for (int k = 1; k <= 6; ++k)
            glow = max(glow, at(uv + vec2(0.0, float(k) * 0.006)).rgb * (1.0 - float(k) / 7.0));
        c.rgb = max(c.rgb, glow * (0.6 + 0.4 * i));
    }
    fragColor = vec4(clamp(c.rgb, 0.0, 1.0) * c.a, c.a) * qt_Opacity;
}
