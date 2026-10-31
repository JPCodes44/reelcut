#pragma once

#include <QImage>
#include <QString>

#include "project.h"

// Fisheye text: a text overlay seen through a bulging lens, biggest in the
// middle and shrinking toward its edges.
//
// The lens is an ellipse Padding times the size of the text block, centred
// on it, so the text's corners stay inside it. A point at normalized radius r
// (0 at the centre, 1 on the ellipse) shows the text from radius
//     r * (1 - k (1 - r^2)),      k = MaxBulge * overlay.bulge
// which magnifies the centre 1 / (1 - k) times and eases back to 1:1 at the
// rim, with no seam. src/shaders/fisheye.frag draws the same lens live in the
// preview; render() draws it for the export, which lays the image over the
// video with ffmpeg's overlay filter (the .ass file can't bend text).
namespace FisheyeText {

constexpr double Padding = 1.45;
constexpr double MaxBulge = 0.6;
// Drop shadow: its direction (times Overlay::shadowDistanceOf()) and softness
// in pixels at 1080x1920.
constexpr double ShadowX = 0.66;
constexpr double ShadowY = 1.0;
constexpr int ShadowBlur = 5;
constexpr int BoxPadding = 18;   // as the .ass text box (src/captions.cpp)
constexpr int BoxRadius = 12;
// Supersampling for sharp text (see render()); the preview's lens texture is
// FlatScale times its size too.
constexpr int FlatScale = 4;
constexpr int LensScale = 2;


// The whole 1080x1920 frame, transparent except for the warped text centred on
// the overlay's position, in the overlay's colors (Overlay::colorOf): fill,
// outline, a box behind it, and the shadow under it.
QImage render(const Overlay &overlay);

}  // namespace FisheyeText
