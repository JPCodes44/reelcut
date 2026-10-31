#pragma once

#include <QJsonObject>
#include <QString>
#include <QVariantMap>

#include <array>

// Primary color correction, laid out like DaVinci Resolve's Color page:
// four color wheels (Lift, Gamma, Gain, Offset), each a puck that pushes the
// picture toward a hue plus a master ring that moves all three channels, and
// Contrast/Pivot, Saturation, Hue, Temperature and Tint.
//
// The same Grade sits on each clip and once on the whole reel (Look). For
// export it is baked into a .cube 3D LUT that ffmpeg's lut3d applies; the
// preview runs the identical formula in src/shaders/grade.frag.
//
// For a channel value x (0..1), in this order:
//   white balance  x *= wb                      temperature / tint
//   offset         x += off                     shifts everything
//   lift           x  = lift + x (1 - lift)     moves blacks, white stays put
//   gain           x *= gain                    moves whites, black stays put
//   gamma          x  = x ^ gexp                bends the middle
//   contrast       x  = (x - pivot) contrast + pivot
//   hue            rotate around the grey axis
//   saturation     x  = Y + (x - Y) sat         Y = Rec.709 luma
namespace ColorGrade {

struct Wheel {
    double x = 0.0;       // puck position in the unit disc, y up
    double y = 0.0;
    double master = 0.0;  // the ring: -1..1
    bool isNeutral() const { return x == 0.0 && y == 0.0 && master == 0.0; }
};

struct Grade {
    Wheel lift, gamma, gain, offset;
    double contrast = 1.0;  // 0..2
    double pivot = 0.435;   // 0..1, Resolve's default
    double saturation = 1.0;  // 0..2
    double hue = 0.0;         // degrees, -180..180
    double temperature = 0.0;  // -1 (cool) .. 1 (warm)
    double tint = 0.0;         // -1 (green) .. 1 (magenta)

    bool isNeutral() const;
    QJsonObject toJson() const;
    static Grade fromJson(const QJsonObject &json);
    QVariantMap toMap() const;  // for QML: the values plus "params"
    // Keys: "lift.x", "gamma.master", ..., "contrast", "pivot", "saturation",
    // "hue", "temperature", "tint"; "reset" (everything) or "reset.lift" etc.
    bool set(const QString &key, double value);
};

// What the formula above uses, worked out once per grade.
struct Params {
    std::array<double, 3> wb{1, 1, 1}, off{0, 0, 0}, lift{0, 0, 0}, gain{1, 1, 1}, gexp{1, 1, 1};
    double contrast = 1.0, pivot = 0.435, saturation = 1.0, hue = 0.0;  // hue in radians
};
Params params(const Grade &grade);

// A wheel's puck as an RGB push that adds up to zero: the hue the puck points
// at (red at the top, then clockwise green, blue), scaled by its distance.
std::array<double, 3> balance(const Wheel &wheel);

std::array<double, 3> apply(const Params &p, std::array<double, 3> rgb);

QString cubeText(const Grade &grade, int size = 33);
bool writeCube(const Grade &grade, const QString &path);

}  // namespace ColorGrade
