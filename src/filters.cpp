#include "filters.h"

#include <QFile>
#include <QTextStream>

#include <cmath>

namespace Filters {
namespace {

double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

// Rec. 709 luma: how bright a color looks.
double luma(const Rgb &c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }

double smoothstep(double x) { return x * x * (3.0 - 2.0 * x); }

// Contrast as a blend toward (or, when negative, away from) a smoothstep
// S-curve, which keeps pure black and white where they are.
double contrastCurve(double x, double amount) {
    const double s = smoothstep(clamp01(x));
    return clamp01(x + (s - x) * amount * 2.0);
}

double channel(double x, double lift, double gamma, double gain) {
    double y = gain * (x + lift * (1.0 - x));
    y = clamp01(y);
    return std::pow(y, 1.0 / (gamma > 0.05 ? gamma : 0.05));
}

Rgb lerp(const Rgb &a, const Rgb &b, double t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

// Builders, so the table below reads as a list of looks.
Grade make(std::initializer_list<double> lift, std::initializer_list<double> gamma, std::initializer_list<double> gain) {
    Grade g;
    std::copy(lift.begin(), lift.end(), g.lift);
    std::copy(gamma.begin(), gamma.end(), g.gamma);
    std::copy(gain.begin(), gain.end(), g.gain);
    return g;
}

QVector<Look> buildLooks() {
    QVector<Look> list;
    auto add = [&](const char *id, const char *label, Grade g) { list.append({id, label, g}); };

    {   // Clean and bright, a touch of contrast.
        Grade g = make({0, 0, 0}, {1.05, 1.05, 1.05}, {1.03, 1.03, 1.03});
        g.contrast = 0.15; g.saturation = 1.08;
        add("daylight", "Daylight", g);
    }
    {   // Warm late sun: amber highlights, soft shadows.
        Grade g = make({0.02, 0.01, 0}, {1.04, 1.0, 0.94}, {1.06, 1.0, 0.88});
        g.highlightTint = {0.06, 0.03, -0.02}; g.contrast = 0.1; g.saturation = 1.05;
        add("golden-hour", "Golden Hour", g);
    }
    {   // Cool shadows, warm skin: the classic two-tone split.
        Grade g;
        g.shadowTint = {-0.05, 0.02, 0.06}; g.highlightTint = {0.06, 0.02, -0.04};
        g.contrast = 0.2; g.saturation = 1.1;
        add("harbor", "Harbor", g);
    }
    {   // Lifted blacks and muted color, like an old print.
        Grade g = make({0.02, 0.02, 0.03}, {1.0, 1.0, 1.0}, {0.97, 0.97, 0.95});
        g.fade = 0.12; g.saturation = 0.75; g.contrast = -0.1;
        add("faded-print", "Faded Print", g);
    }
    {   // High-contrast black and white.
        Grade g; g.mono = 1.0; g.contrast = 0.35;
        add("silver", "Silver", g);
    }
    {   // Soft black and white with grey blacks.
        Grade g; g.mono = 1.0; g.fade = 0.1; g.contrast = -0.05;
        add("charcoal", "Charcoal", g);
    }
    {   // Brown-toned monochrome.
        Grade g; g.mono = 1.0; g.monoTint = {1.08, 0.94, 0.74}; g.contrast = 0.1; g.fade = 0.04;
        add("sepia-ink", "Sepia Ink", g);
    }
    {   // City at night: blue shadows, magenta-leaning highlights.
        Grade g = make({0, 0, 0.03}, {0.95, 0.98, 1.08}, {1.0, 0.96, 1.04});
        g.shadowTint = {-0.02, 0, 0.08}; g.highlightTint = {0.05, -0.02, 0.04};
        g.contrast = 0.25; g.saturation = 1.15;
        add("night-city", "Night City", g);
    }
    {   // Fresh green-cyan cast, gentle.
        Grade g = make({0, 0.02, 0.01}, {0.98, 1.04, 1.0}, {0.96, 1.02, 1.0});
        g.highlightTint = {-0.02, 0.03, 0.01}; g.saturation = 0.95;
        add("mint", "Mint", g);
    }
    {   // Pink highlights, airy.
        Grade g = make({0.02, 0.01, 0.02}, {1.04, 1.0, 1.02}, {1.03, 0.98, 1.0});
        g.highlightTint = {0.05, 0, 0.03}; g.fade = 0.04; g.saturation = 0.95;
        add("rosewater", "Rosewater", g);
    }
    {   // Dry orange earth, faded sky.
        Grade g = make({0.02, 0.01, 0}, {1.06, 1.0, 0.92}, {1.04, 0.98, 0.86});
        g.contrast = 0.15; g.saturation = 0.9; g.shadowTint = {0.03, 0.01, -0.02};
        add("desert", "Desert", g);
    }
    {   // Icy blue, slightly desaturated.
        Grade g = make({0, 0.01, 0.03}, {0.96, 1.0, 1.06}, {0.94, 1.0, 1.06});
        g.saturation = 0.85; g.contrast = 0.05; g.highlightTint = {-0.03, 0, 0.04};
        add("arctic", "Arctic", g);
    }
    {   // Deep greens, muted reds.
        Grade g = make({0, 0.01, 0}, {0.96, 1.05, 0.98}, {0.95, 1.0, 0.94});
        g.shadowTint = {-0.02, 0.03, 0}; g.saturation = 0.95; g.contrast = 0.15;
        add("moss", "Moss", g);
    }
    {   // Metallic orange highlights, teal shadows, strong.
        Grade g = make({0, 0.01, 0.02}, {1.05, 1.0, 0.95}, {1.08, 0.98, 0.9});
        g.shadowTint = {-0.04, 0.02, 0.05}; g.highlightTint = {0.08, 0.03, -0.04};
        g.contrast = 0.3; g.saturation = 1.0;
        add("copper", "Copper", g);
    }
    {   // Grey day: low contrast, low saturation.
        Grade g; g.contrast = -0.2; g.saturation = 0.7; g.fade = 0.05;
        add("overcast", "Overcast", g);
    }
    {   // Bold color and contrast for short-form.
        Grade g; g.contrast = 0.4; g.saturation = 1.35;
        add("punch", "Punch", g);
    }
    {   // Purple-tinted haze.
        Grade g = make({0.03, 0.01, 0.04}, {1.02, 0.98, 1.04}, {1.0, 0.96, 1.02});
        g.fade = 0.08; g.highlightTint = {0.03, -0.01, 0.05}; g.saturation = 0.9;
        add("lavender-haze", "Lavender Haze", g);
    }
    {   // Film-lab cross processing: yellow highs, blue lows, punchy.
        Grade g = make({0, 0, 0.06}, {0.92, 1.05, 1.15}, {1.04, 1.05, 0.88});
        g.contrast = 0.3; g.saturation = 1.2;
        add("cross-process", "Cross Process", g);
    }
    {   // Silver retained in the print: harsh contrast, little color.
        Grade g; g.contrast = 0.45; g.saturation = 0.45;
        add("bleach-bypass", "Bleach Bypass", g);
    }
    {   // Warm interior tungsten corrected toward neutral, cozy.
        Grade g = make({0.01, 0.005, 0}, {1.03, 1.0, 0.97}, {1.02, 0.99, 0.95});
        g.fade = 0.03; g.contrast = 0.08; g.shadowTint = {0.02, 0.01, 0};
        add("cozy", "Cozy", g);
    }
    return list;
}
}  // namespace

const QVector<Look> &looks() {
    static const QVector<Look> list = buildLooks();
    return list;
}

QStringList ids() {
    QStringList out;
    for (const Look &look : looks())
        out << look.id;
    return out;
}

const Look *find(const QString &id) {
    for (const Look &look : looks())
        if (look.id.compare(id, Qt::CaseInsensitive) == 0)
            return &look;
    return nullptr;
}

Rgb grade(const Grade &g, Rgb c) {
    c = {contrastCurve(c.r, g.contrast), contrastCurve(c.g, g.contrast), contrastCurve(c.b, g.contrast)};
    c = {channel(c.r, g.lift[0], g.gamma[0], g.gain[0]), channel(c.g, g.lift[1], g.gamma[1], g.gain[1]),
         channel(c.b, g.lift[2], g.gamma[2], g.gain[2])};

    // Split toning: tints weighted toward the darks and the brights.
    const double l = luma(c);
    const double dark = (1.0 - l) * (1.0 - l);
    const double bright = l * l;
    c.r += g.shadowTint.r * dark + g.highlightTint.r * bright;
    c.g += g.shadowTint.g * dark + g.highlightTint.g * bright;
    c.b += g.shadowTint.b * dark + g.highlightTint.b * bright;

    const double l2 = luma(c);
    const Rgb grey{l2, l2, l2};
    c = lerp(grey, c, g.saturation);
    const Rgb toned{l2 * g.monoTint.r, l2 * g.monoTint.g, l2 * g.monoTint.b};
    c = lerp(c, toned, g.mono);

    auto fade = [&](double v) { return g.fade + clamp01(v) * (1.0 - g.fade); };
    return {fade(c.r), fade(c.g), fade(c.b)};
}

Rgb apply(const Look &look, Rgb in, double amount) {
    return lerp(in, grade(look.grade, in), clamp01(amount));
}

QString cubeText(const Look &look, double amount, int size) {
    QString text;
    QTextStream out(&text);
    out << "TITLE \"Reelcut " << look.label << "\"\n";
    out << "LUT_3D_SIZE " << size << "\n";
    const double step = 1.0 / (size - 1);
    // .cube order: red changes fastest, then green, then blue.
    for (int b = 0; b < size; ++b)
        for (int g = 0; g < size; ++g)
            for (int r = 0; r < size; ++r) {
                const Rgb o = apply(look, {r * step, g * step, b * step}, amount);
                out << QString::number(o.r, 'f', 6) << ' ' << QString::number(o.g, 'f', 6) << ' '
                    << QString::number(o.b, 'f', 6) << '\n';
            }
    return text;
}

bool writeCube(const Look &look, double amount, const QString &path, int size) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(cubeText(look, amount, size).toUtf8()) > 0;
}

QImage stripImage(const Look &look) {
    // 64 levels per channel. Blue picks the tile (8 across, 8 down);
    // red runs left to right inside a tile, green top to bottom.
    QImage image(512, 512, QImage::Format_RGB32);
    for (int b = 0; b < 64; ++b) {
        const int tileX = (b % 8) * 64;
        const int tileY = (b / 8) * 64;
        for (int g = 0; g < 64; ++g) {
            auto *row = reinterpret_cast<QRgb *>(image.scanLine(tileY + g));
            for (int r = 0; r < 64; ++r) {
                const Rgb o = grade(look.grade, {r / 63.0, g / 63.0, b / 63.0});
                row[tileX + r] = qRgb(qRound(clamp01(o.r) * 255), qRound(clamp01(o.g) * 255), qRound(clamp01(o.b) * 255));
            }
        }
    }
    return image;
}

QVector<QColor> swatch(const Look &look) {
    const Rgb samples[] = {{0.86, 0.64, 0.52}, {0.45, 0.66, 0.88}, {0.34, 0.52, 0.27}};
    QVector<QColor> out;
    for (const Rgb &s : samples) {
        const Rgb o = grade(look.grade, s);
        out << QColor::fromRgbF(clamp01(o.r), clamp01(o.g), clamp01(o.b));
    }
    return out;
}

}  // namespace Filters
