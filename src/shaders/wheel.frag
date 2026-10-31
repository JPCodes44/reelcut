#version 440
// A color wheel's face: hue around the circle (red at the top, clockwise to
// green and blue, matching ColorGrade::balance()), white-ish at the centre,
// full color at the rim. `value` dims it, for the text color picker.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float value;     // brightness of the face, 0..1
    float strength;  // how saturated the rim is, 0..1
};

vec3 hueColor(float h) {
    vec3 k = vec3(0.0, 4.0, 2.0);
    return clamp(abs(mod(h / 60.0 + k, 6.0) - 3.0) - 1.0, 0.0, 1.0);
}

void main() {
    vec2 d = qt_TexCoord0 * 2.0 - 1.0;
    d.y = -d.y;  // y up, like the puck
    float r = length(d);
    float edge = fwidth(r);
    float inside = 1.0 - smoothstep(1.0 - edge, 1.0, r);
    float angle = degrees(atan(d.y, d.x));
    vec3 c = mix(vec3(1.0), hueColor(90.0 - angle), clamp(r, 0.0, 1.0) * strength) * value;
    fragColor = vec4(c, 1.0) * inside * qt_Opacity;
}
