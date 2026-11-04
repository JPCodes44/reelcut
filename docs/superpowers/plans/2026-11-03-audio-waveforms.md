# Audio Waveforms Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Show detailed waveforms for music and every video clip with audio.

**Architecture:** Render viewport-sized tiles asynchronously from the source's native audio using streaming native PCM peak envelopes. Cache tiles and regenerate them at each zoom level and device pixel density. Separate channels prevent phase cancellation. Source start and duration include trims, speed and music offset.

**Tech Stack:** Qt Quick, C++17, QQuickAsyncImageProvider, FFmpeg PCM decoding and QPainter.

**Spec:** User-approved design in conversation: both music and video audio; zoom reveals finer source detail; background generation keeps the editor responsive.

## Global Constraints

- Preserve existing uncommitted changes and project media.
- No resampling of source audio for display; exact sample-to-pixel peak aggregation rather than averaging.
- Bound workers, decoded image cache and tile dimensions; cancel obsolete requests.
- Waveforms are editor decoration and never appear in exports.

## Review Focus

- Stereo phase cancellation: opposite channels must remain visible.
- Narrow transients: peaks survive zoomed-out aggregation.
- Trim/speed/offset: waveforms must map to the audible source interval.
- Long files and high zoom: render only visible tiles, bounded cache and jobs.
- Missing or silent media and audio tails: empty display, no UI stall.

### Task 1: Peak rendering and async provider

Files: src/waveformprovider.{h,cpp}, reelcut.pro, tests/reelcut_tests.{cpp,pro}.

- [x] Write failing tests with generated stereo impulses, offset ranges, silence and cancellation.
- [x] Run those tests and confirm failure.
- [x] Implement waveform::render(path, start, duration, size, color, cancelled) and WaveformProvider.
- [x] Verify tests and source path encoding, cache invalidation and cancellation.

### Task 2: Timeline integration

Files: src/Waveform.qml, src/ReelTimeline.qml, src/Main.qml, src/main.cpp, src/resources.qrc.

- [x] Add tiled Waveform component with viewport-only requests and physical-pixel sizes.
- [x] Add music and clip audio displays with source-time mapping and readable labels.
- [x] Increase music lane height and corresponding timeline height.
- [x] Add real QML integration coverage and run full suite/build.
- [x] Verify running build displays current project's waveform when possible.

## Verification record

- Native stereo impulse tests: RED (empty stub) → GREEN.
- High-zoom sample-placement test: RED (integer bins misplaced native samples) → GREEN (fractional sample intervals).
- Five waveform tests pass, covering stereo phase, single-sample peaks, high zoom, silence, missing files, cancellation, Unicode paths, viewport bounds, music offset and clip trims/speed.
- App builds; offscreen screenshot of current letters reel confirms music waveform display.
- Final review found high-zoom placement/blank-tile issue; fixed with streaming PCM peak calculation and regression tests.
- Full suite initially: 70 passed, one previewItemsDragAndResize redo assertion failed. The failing Ctrl+Y shortcut code is unchanged by this feature.

Final verification: application build succeeds; full suite 72 passed, 1 failed (previewItemsDragAndResize, Ctrl+Y redo assertion); all five waveform feature tests pass. The installed /usr/bin/reelcut is not writable by the current user, so the updated executable is build/reelcut. Existing user changes were preserved; no commits or system installation performed.

## Follow-up: vertical music resizing

User requested dragging the music lane vertically to enlarge its waveform. Added an 8px top-edge grip with vertical resize cursor, scene-coordinate drag tracking, 52–320px music lane bounds, and timeline height bound to its content. Raised waveform render height limit to 2048 physical pixels for enlarged high-DPI displays. Added real pointer-drag regression coverage for growth, shrinkage, bounds and unchanged project data; extended native peak test to an 800px render height. Six waveform/resize tests pass (8 including test setup/cleanup); application build and diff checks pass.
