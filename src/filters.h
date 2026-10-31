#pragma once

#include <QColor>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>

// The filter library: named color looks, each a small set of grading
// controls. A look is turned into a 3D lookup table (LUT) two ways:
//
//  - a .cube file for ffmpeg's lut3d filter, used on export;
//  - a 512x512 "strip" image for the live preview shader: 64 blue slices of
//    64x64 red/green cells, laid out 8 across and 8 down.
//
// Both sample the same grade() function, so preview and export agree.
// See docs/effects.md for how a 3D LUT works.
namespace Filters {

struct Rgb {
    double r = 0.0, g = 0.0, b = 0.0;
};

// Grading controls, applied in this order by grade().
struct Grade {
    double contrast = 0.0;                 // -1..1, an S-curve around mid grey
    double lift[3] = {0, 0, 0};            // raises the darks per channel
    double gamma[3] = {1, 1, 1};           // mid-tone bend per channel
    double gain[3] = {1, 1, 1};            // scales the brights per channel
    Rgb shadowTint;                        // split toning: added to the darks...
    Rgb highlightTint;                     // ...and to the brights
    double saturation = 1.0;               // 0 is grey, 1 untouched
    double mono = 0.0;                     // 0..1 toward a tinted monochrome
    Rgb monoTint{1, 1, 1};
    double fade = 0.0;                     // lifts black toward grey, like old print
};

struct Look {
    QString id;      // stable key stored in projects, e.g. "golden-hour"
    QString label;   // what the editor shows
    Grade grade;
};

const QVector<Look> &looks();
QStringList ids();
// nullptr when the id is unknown.
const Look *find(const QString &id);

// One color through a grade; inputs and outputs are 0..1.
Rgb grade(const Grade &g, Rgb in);
// The look at `amount` (0 = untouched, 1 = full).
Rgb apply(const Look &look, Rgb in, double amount);

// A .cube file of the look at `amount`, `size` points per axis.
QString cubeText(const Look &look, double amount, int size = 33);
bool writeCube(const Look &look, double amount, const QString &path, int size = 33);

// The 512x512 strip LUT of the look at full strength (the shader mixes
// in the amount, so the image only changes when the look does).
QImage stripImage(const Look &look);

// Three sample colors (skin, sky, foliage) through the look, for the
// swatches in the editor.
QVector<QColor> swatch(const Look &look);

}  // namespace Filters
