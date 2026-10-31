# Reelcut — design

Reelcut is an Omarchy desktop app for making vertical short-form videos (Instagram Reels, TikTok).
It covers the parts of DaVinci Resolve that matter for a 15–90 second reel: cutting, captions,
color, music and export. It is built like Omarchy's own apps: C++ and Qt Quick, one binary,
packaged with a PKGBUILD, following the live Omarchy theme.

## Goals (v1)

1. **Import** video files (drag and drop or file picker). Every clip is fit into a 1080×1920 frame.
   Landscape clips are center-cropped by default; each clip has zoom (1.0–3.0) and pan X/Y (-1..1).
2. **Timeline**: one video track (ordered clips) and one music track. Split at playhead, trim in/out,
   reorder, delete, speed 0.25×–4× per clip. Full undo/redo.
3. **Auto-captions**: local transcription with whisper.cpp (`whisper-cli`), word-level timings.
   Words are editable. Caption styles: `bold` (white, black outline), `highlight` (current word
   colored), `pop` (one word at a time, scaled), `box` (white text on dark box). Captions are burned in.
4. **Silence cut**: detect silences with ffmpeg `silencedetect` on a clip and split out/remove them
   (threshold -35 dB, minimum 0.4 s, keep 0.1 s padding). One undoable action.
5. **Text overlays**: hook text with start/end time, vertical position, size and an in animation
   (`none`, `fade`, `pop`, `slide`).
6. **Color**: project-wide look: exposure, contrast, saturation, temperature sliders, plus an optional
   `.cube` LUT (works with Resolve LUTs) and a handful of built-in presets.
7. **Audio**: music track with volume; auto-ducking of music under clip audio; loudness normalized
   to -14 LUFS on export.
8. **Export**: presets `Instagram Reels` and `TikTok` (1080×1920, 30 fps, H.264, AAC 48 kHz).
   Uses `h264_nvenc` when available, falls back to `libx264`. Progress bar and cancel.
   Preview shows a platform safe-zone overlay.

## Non-goals (v1)

Multi-track video compositing, keyframes, transitions, beat sync, face-tracking reframe,
templates, direct upload to platforms.

## Architecture

```
QML UI: MediaBin | Preview (9:16 + safe zones) | Inspector
        Timeline
             │ Q_PROPERTY / Q_INVOKABLE / signals
C++ core:
  Project          edit model, undo stack, JSON (.reel) save/load
  ExportGraph      Project → ffmpeg argument list (pure function)
  CaptionRenderer  words + style → .ass subtitle text (pure function)
  SilenceParser    ffmpeg silencedetect log → silence ranges (pure function)
  Transcriber      runs ffmpeg (16 kHz wav) + whisper-cli, parses JSON word timings
  SilenceDetector  runs ffmpeg silencedetect, uses SilenceParser
  Exporter         runs ffmpeg with ExportGraph args, parses progress
  MediaProbe       ffprobe → duration, size, has-audio
```

### Units

- **Project** (`src/project.*`): owns `QVector<Clip>`, `MusicTrack`, `QVector<TextOverlay>`,
  `QVector<Word>` captions + caption style, `ColorLook`. Mutations go through `QUndoStack` commands.
  Exposes a `ClipModel` (QAbstractListModel) to QML. Times are stored in seconds (double).
  Timeline time of clip *i* = sum of `(out - in) / speed` of earlier clips.
- **ExportGraph** (`src/exportgraph.*`): builds the ffmpeg `filter_complex`:
  per clip `trim/atrim → setpts/atempo (speed) → scale+crop (zoom/pan) → fps=30`, `concat`,
  then `eq` (exposure/contrast/saturation), `colortemperature`, `lut3d`, `ass` (captions + text
  overlays rendered into one .ass file), audio: clip audio (silent `anullsrc` for clips without
  audio) concatenated, music `volume` + `sidechaincompress` keyed by voice, `amix`, `loudnorm=I=-14`.
- **CaptionRenderer** (`src/captionrenderer.*`): groups words into lines of ≤ 3 words / ≤ 18 chars,
  writes ASS events per style. Text overlays are additional ASS events with animation tags
  (`\fad`, `\t(\fscx\fscy)`, `\move`).
- **Preview**: QML `MediaPlayer` + `VideoOutput` plays the current clip's source in the clip's in/out
  range at its speed, advancing to the next clip; a `ShaderEffect` applies the color look; captions and
  text overlays are QML `Text` layers driven by the same timing data. LUT is applied in export only;
  the preview shows a "LUT on export" badge when a LUT is set.
- **Background jobs** (Transcriber, SilenceDetector, Exporter) run `QProcess`, never block the UI,
  report progress, can be cancelled.

### Data flow

Import → `MediaProbe` → `Project.addClip` → Timeline/Preview update via models.
Captions: `Transcriber` → words (source time) → mapped to timeline time → `Project.setCaptions`.
Export: `ExportGraph.build(project)` → `.ass` written to a temp dir → `Exporter` runs ffmpeg → mp4.

### Error handling

- Missing `whisper-cli` or model: captions panel explains the install command; no crash.
- Model download on first use to `~/.local/share/reelcut/models/ggml-base.en.bin` (with progress).
- Missing source file on load: clip marked offline (red), export disabled until fixed.
- ffmpeg failure: export dialog shows the last 40 lines of stderr.
- NVENC unavailable: retry with libx264 automatically.

## Testing

Qt Test (`tests/reelcut_tests.cpp`), run with `bin/test`:
- `ExportGraph`: golden tests for filter strings (speed, crop, color, music, no-audio clip).
- `CaptionRenderer`: line grouping and ASS timestamps.
- `SilenceParser`: parsing of real silencedetect output; keep-range computation with padding.
- `Project`: timeline time mapping, split, undo/redo, JSON round trip.
- End-to-end: generate a 3 s test clip with ffmpeg `testsrc`/`sine`, export, ffprobe the result
  is 1080×1920, ~3 s, has audio.

## Packaging

qmake6 + make via `bin/build`, single binary `reelcut`, `.desktop` file, SVG icon, PKGBUILD in `pkgbuild/`, installed with `bin/install`. Project lives in `~/Work/reelcut` and follows the omarchy-app skill (live theme accent and rounding, Translator with `qsTr`, portal file picker, `?` help overlay, `Q` quit with unsaved-work prompt).
Runtime deps: qt6-base, qt6-declarative, qt6-multimedia, ffmpeg. Optional: `whisper-cpp` from extra (captions; binary `whisper-cli`).
