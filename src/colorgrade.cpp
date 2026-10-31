#include "colorgrade.h"

#include <QFile>
#include <QStringList>
#include <QVariantList>

#include <algorithm>
#include <cmath>

namespace ColorGrade {

namespace {

constexpr double Pi = 3.14159265358979323846;

QJsonObject wheelJson(const Wheel &w) { return {{"x", w.x}, {"y", w.y}, {"master", w.master}}; }

Wheel wheelFrom(const QJsonObject &o) {
    Wheel w;
    w.x = qBound(-1.0, o.value("x").toDouble(), 1.0);
    w.y = qBound(-1.0, o.value("y").toDouble(), 1.0);
    w.master = qBound(-1.0, o.value("master").toDouble(), 1.0);
    return w;
}

QVariantList list(const std::array<double, 3> &a) { return {a[0], a[1], a[2]}; }

// Fully saturated color of hue h (degrees), 0..1 per channel.
std::array<double, 3> hueColor(double h) {
    h = std::fmod(std::fmod(h, 360.0) + 360.0, 360.0) / 60.0;
    const double x = 1.0 - std::fabs(std::fmod(h, 2.0) - 1.0);
    switch (int(h)) {
    case 0: return {1, x, 0};
    case 1: return {x, 1, 0};
    case 2: return {0, 1, x};
    case 3: return {0, x, 1};
    case 4: return {x, 0, 1};
    default: return {1, 0, x};
    }
}

}  // namespace

bool Grade::isNeutral() const {
    return lift.isNeutral() && gamma.isNeutral() && gain.isNeutral() && offset.isNeutral() && contrast == 1.0
           && saturation == 1.0 && hue == 0.0 && temperature == 0.0 && tint == 0.0;
}

QJsonObject Grade::toJson() const {
    return {{"lift", wheelJson(lift)}, {"gamma", wheelJson(gamma)}, {"gain", wheelJson(gain)},
            {"offset", wheelJson(offset)}, {"contrast", contrast}, {"pivot", pivot},
            {"saturation", saturation}, {"hue", hue}, {"temperature", temperature}, {"tint", tint}};
}

Grade Grade::fromJson(const QJsonObject &json) {
    Grade g;
    g.lift = wheelFrom(json.value("lift").toObject());
    g.gamma = wheelFrom(json.value("gamma").toObject());
    g.gain = wheelFrom(json.value("gain").toObject());
    g.offset = wheelFrom(json.value("offset").toObject());
    g.contrast = qBound(0.0, json.value("contrast").toDouble(1.0), 2.0);
    g.pivot = qBound(0.0, json.value("pivot").toDouble(0.435), 1.0);
    g.saturation = qBound(0.0, json.value("saturation").toDouble(1.0), 2.0);
    g.hue = qBound(-180.0, json.value("hue").toDouble(), 180.0);
    g.temperature = qBound(-1.0, json.value("temperature").toDouble(), 1.0);
    g.tint = qBound(-1.0, json.value("tint").toDouble(), 1.0);
    return g;
}

QVariantMap Grade::toMap() const {
    QVariantMap map = toJson().toVariantMap();
    const Params p = params(*this);
    // What each wheel does to R, G, B (Resolve shows these under each wheel).
    map["params"] = QVariantMap{{"wb", list(p.wb)}, {"off", list(p.off)}, {"lift", list(p.lift)},
                                {"gain", list(p.gain)}, {"gexp", list(p.gexp)}, {"contrast", p.contrast},
                                {"pivot", p.pivot}, {"saturation", p.saturation}, {"hue", p.hue}};
    map["neutral"] = isNeutral();
    return map;
}

bool Grade::set(const QString &key, double value) {
    if (key == "reset") {
        *this = Grade();
        return true;
    }
    const QStringList parts = key.split('.');
    Wheel *wheel = nullptr;
    const QString name = parts.value(parts.first() == "reset" ? 1 : 0);
    if (name == "lift") wheel = &lift;
    else if (name == "gamma") wheel = &gamma;
    else if (name == "gain") wheel = &gain;
    else if (name == "offset") wheel = &offset;
    if (parts.first() == "reset") {
        if (!wheel)
            return false;
        *wheel = Wheel();
        return true;
    }
    if (wheel && parts.size() == 2) {
        if (parts[1] == "x") wheel->x = qBound(-1.0, value, 1.0);
        else if (parts[1] == "y") wheel->y = qBound(-1.0, value, 1.0);
        else if (parts[1] == "master") wheel->master = qBound(-1.0, value, 1.0);
        else return false;
        // Keep the puck inside its disc.
        const double r = std::hypot(wheel->x, wheel->y);
        if (r > 1.0) {
            wheel->x /= r;
            wheel->y /= r;
        }
        return true;
    }
    if (key == "contrast") contrast = qBound(0.0, value, 2.0);
    else if (key == "pivot") pivot = qBound(0.0, value, 1.0);
    else if (key == "saturation") saturation = qBound(0.0, value, 2.0);
    else if (key == "hue") hue = qBound(-180.0, value, 180.0);
    else if (key == "temperature") temperature = qBound(-1.0, value, 1.0);
    else if (key == "tint") tint = qBound(-1.0, value, 1.0);
    else return false;
    return true;
}

std::array<double, 3> balance(const Wheel &w) {
    const double r = std::hypot(w.x, w.y);
    if (r == 0.0)
        return {0, 0, 0};
    // Red at the top of the wheel, hue growing clockwise (screen y is down,
    // but the puck's y is up): angle 90 degrees is hue 0.
    const double angle = std::atan2(w.y, w.x) * 180.0 / Pi;
    const std::array<double, 3> c = hueColor(90.0 - angle);
    const double mean = (c[0] + c[1] + c[2]) / 3.0;
    return {r * (c[0] - mean), r * (c[1] - mean), r * (c[2] - mean)};
}

Params params(const Grade &g) {
    Params p;
    const auto bl = balance(g.lift), bg = balance(g.gamma), bn = balance(g.gain), bo = balance(g.offset);
    for (int c = 0; c < 3; ++c) {
        p.lift[c] = 0.5 * g.lift.master + 0.35 * bl[c];
        p.gexp[c] = std::exp(-1.2 * (0.8 * g.gamma.master + 0.35 * bg[c]));
        p.gain[c] = 1.0 + g.gain.master + 0.5 * bn[c];
        p.off[c] = 0.25 * g.offset.master + 0.2 * bo[c];
    }
    p.wb = {1.0 + 0.2 * g.temperature, 1.0 - 0.2 * g.tint, 1.0 - 0.2 * g.temperature};
    p.contrast = g.contrast;
    p.pivot = g.pivot;
    p.saturation = g.saturation;
    p.hue = g.hue * Pi / 180.0;
    return p;
}

std::array<double, 3> apply(const Params &p, std::array<double, 3> v) {
    for (int c = 0; c < 3; ++c) {
        double x = v[c] * p.wb[c] + p.off[c];
        x = p.lift[c] + x * (1.0 - p.lift[c]);
        x *= p.gain[c];
        x = std::pow(std::max(x, 0.0), p.gexp[c]);
        v[c] = (x - p.pivot) * p.contrast + p.pivot;
    }
    if (p.hue != 0.0) {
        // Rodrigues' rotation around the grey axis (1,1,1)/sqrt(3).
        const double k = 1.0 / std::sqrt(3.0), cs = std::cos(p.hue), sn = std::sin(p.hue);
        const double dot = (v[0] + v[1] + v[2]) * k;
        const std::array<double, 3> cross{k * (v[2] - v[1]), k * (v[0] - v[2]), k * (v[1] - v[0])};
        for (int c = 0; c < 3; ++c)
            v[c] = v[c] * cs + cross[c] * sn + k * dot * (1.0 - cs);
    }
    const double y = 0.2126 * v[0] + 0.7152 * v[1] + 0.0722 * v[2];
    for (double &x : v)
        x = std::clamp(y + (x - y) * p.saturation, 0.0, 1.0);
    return v;
}

QString cubeText(const Grade &grade, int size) {
    const Params p = params(grade);
    QString out = QString("TITLE \"Reelcut color wheels\"\nLUT_3D_SIZE %1\n").arg(size);
    out.reserve(out.size() + size * size * size * 28);
    // .cube order: red changes fastest, then green, then blue.
    for (int b = 0; b < size; ++b)
        for (int g = 0; g < size; ++g)
            for (int r = 0; r < size; ++r) {
                const auto v = apply(p, {double(r) / (size - 1), double(g) / (size - 1), double(b) / (size - 1)});
                out += QString("%1 %2 %3\n").arg(v[0], 0, 'f', 6).arg(v[1], 0, 'f', 6).arg(v[2], 0, 'f', 6);
            }
    return out;
}

bool writeCube(const Grade &grade, const QString &path) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(cubeText(grade).toUtf8()) > 0;
}

}  // namespace ColorGrade
