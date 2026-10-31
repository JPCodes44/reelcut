#pragma once

#include <QString>

#include <functional>
#include <memory>

#include "project.h"

// Finding the person in a clip, for body effects.
//
// makeMask() decodes the clip exactly as the export frames it (via
// clipVideoChain), shrinks each frame to the model's 192x192 input, asks a
// MaskFn for a person mask, and encodes the masks as a small grey video
// (white = person) next to nothing else: a cache file keyed by everything
// that changes which frames the clip produces.
namespace Segment {

// Size the model sees, and the size of the stored mask video (9:16).
constexpr int ModelSize = 192;
constexpr int MaskWidth = 270;
constexpr int MaskHeight = 480;

// Fills `mask` (ModelSize x ModelSize bytes, 255 = person) from `rgb`
// (ModelSize x ModelSize x 3 bytes). Tests pass a fake one.
using MaskFn = std::function<void(const unsigned char *rgb, unsigned char *mask)>;

// The person segmentation model: OpenCV Zoo's PP-HumanSeg (Apache-2.0),
// downloaded on first use.
QString modelPath();
QString modelUrl();
bool downloadModel(QString *error);

// Loads the model with OpenCV's DNN module; the returned function runs it.
// Empty (and *error set) if it can't load.
MaskFn loadModel(const QString &path, QString *error);

// Where a clip's mask video is cached. Changes whenever the clip's frames would.
QString maskPath(const Clip &clip);
bool hasMask(const Clip &clip);

// Writes the clip's mask video to `output`. `progress` gets 0..1 and returns
// false to cancel.
bool makeMask(const Clip &clip, const MaskFn &fn, const QString &output,
              const std::function<bool(double)> &progress, QString *error);

// Makes every missing mask the project's body effects need, downloading and
// loading the model if any are missing. Blocking.
bool ensureMasks(const Project &project, const std::function<bool(double)> &progress, QString *error);

}  // namespace Segment
