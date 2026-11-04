# Reelcut

Make vertical reels for Instagram and TikTok on Omarchy: cut your clips, caption them automatically, give them a look, add music, and export an MP4 that's ready to post.

![Reelcut editing a reel](docs/screenshot.png)

## What it does

- **Photos and slideshows**: drop in photos (JPG, PNG, WebP, HEIC) and they become slides with a duration and an optional slow zoom or pan. Export the slideshow as a video, or as a **photo set** for TikTok photo posts: one 1080×1920 JPG per slide with the text and look drawn in, numbered in order. Post the set in TikTok's photo mode and pick the sound in the app (TikTok's library sounds can only be added there).
- **Cut**: drop in videos, split at the playhead, trim with the handles, reorder, change speed from 0.25× to 4×.
- **Transitions**: 15 transitions between clips (dissolve, dip to black, flash, wipes, slides, iris, clock, zoom, pixelate, blur and more), with a live preview. [How they work](docs/effects.md#2-transitions-two-pictures-and-a-progress-value).
- **Social preview**: switch Instagram/TikTok to preview its app chrome. Click the caption or **Caption and tags** to edit the saved posting text; **Show app UI** hides the chrome. Text wraps inside layout containers and the app UI never appears in exports.
- **Frame**: every clip fills the 9:16 frame; zoom and pan landscape footage to pick what shows.
- **Cut silences**: one click removes the pauses from a talking clip.
- **Body effects**: background blur, color pop, spotlight, neon outline or a solid backdrop behind the person in a clip. A small segmentation model (OpenCV, downloaded once, about 6 MB) finds the person on your computer. [How they work](docs/effects.md#4-body-effects-a-mask-from-a-segmentation-model).
- **Captions**: transcribed on your computer with whisper.cpp, word by word, in four styles (highlight the spoken word, pop one word at a time, bold, or on a box). Click any word to fix it.
- **Text**: hook titles that pop, fade or slide in.
- **Effects**: 13 video effects on their own track (shake, zoom pulse, strobe, VHS, RGB split, glitch, vignette, blur, film grain, mono flicker, invert, mirror, light trails), each with intensity and speed. [How they work](docs/effects.md#3-video-effects-a-track-of-timed-filters).
- **Look**: a library of 20 filters with a strength slider, exposure, contrast, saturation and temperature, six presets, and any `.cube` LUT (including ones from DaVinci Resolve). [How the filters work](docs/effects.md).
- **Music**: a music track that ducks under speech, and loudness leveled to -14 LUFS.
- **Post and schedule**: publish a reel straight to a connected Instagram Creator or Business account, or tick **Schedule for later**, type a time (`tomorrow 9am`, `fri 18:30`) and it goes out then, even if your computer is off. Scheduling runs on a small Convex backend you deploy yourself; see [scheduler/README.md](scheduler/README.md).
- **Export**: 1080×1920, 30 fps, H.264 and AAC, on the GPU with NVENC when there is one. The preview shows each platform's safe zones so captions don't end up under the buttons.

Projects save as `.reel` files next to your media; the original videos are never changed.

## Install

```sh
bin/install
```

Captions need whisper.cpp, which downloads a 148 MB speech model the first time:

```sh
omarchy-pkg-add whisper-cpp
```

## Shortcuts

| Key | Action |
| --- | --- |
| Space | Play / pause |
| S | Split at playhead |
| ← / → | Step a frame (Shift: a second) |
| Home / End | Go to start / end |
| Delete | Delete what's selected (clip, text or captions) |
| Alt+← / Alt+→ | Move the selected clip |
| I | Add clips |
| T | Add text at the playhead |
| G | Show or hide safe zones |
| Ctrl+Z / Ctrl+Y | Undo / redo (Ctrl+Shift+Z also redoes) |
| Drag in the preview | Move text, captions, or pan the picture; they snap to the center, thirds and safe zone |
| Drag a corner / scroll | Resize text or captions, zoom the picture |
| Ctrl + drag | Move without snapping to the center, thirds or safe zone |
| Drag in the timeline | Reorder clips, move text in time, trim either end |
| Ctrl+S / Ctrl+Shift+S | Save / save as |
| Ctrl+O | Open a project |
| Ctrl+E | Export |
| Ctrl+scroll | Zoom the timeline |
| ? | Show shortcuts |
| Q | Quit |

## From the command line

```sh
reelcut clip1.mp4 clip2.mov        # open the editor with these clips
reelcut my.reel                    # open a project
reelcut export my.reel out.mp4     # render without the window (add --json for scripts)
```

## Let an AI agent edit with you (MCP)

`reelcut mcp` is a [Model Context Protocol](https://modelcontextprotocol.io) server with every editing feature as a tool. Add it to Claude Code:

```sh
claude mcp add reelcut --scope user -- reelcut mcp
```

Then ask for things like "make a reel from these three clips, cut the silences, caption it in pop style, add a hook that says *POV: you edit on Linux*, warm look, export for TikTok". Each edit is saved to the `.reel` file at once, and if that project is open in the Reelcut window it updates live (Ctrl+Z in the window undoes an outside edit). `open_project` with no path picks up whatever the window has open.

| Tool | What it does |
| --- | --- |
| `create_project`, `open_project`, `get_project`, `probe_media`, `set_platform` | Projects and media |
| `add_clips`, `delete_clip`, `move_clip`, `split`, `edit_clip`, `cut_silences` | Cutting, trimming, speed, framing, photo duration and motion |
| `transcribe`, `set_captions`, `edit_words` | Captions |
| `add_text`, `edit_text`, `remove_text` | Text overlays |
| `set_look`, `set_music` | Color, LUTs, music |
| `undo`, `redo` | History |
| `preview_frame` | A PNG of the finished frame at any time |
| `export` | The MP4 |
| `export_photos` | A photo set for TikTok photo posts |
| `show_in_app` | Opens the project in the window |

## Development

```sh
bin/build    # build/reelcut
bin/test     # unit tests, plus real ffmpeg exports
```

`REELCUT_GRAB=shot.png REELCUT_GRAB_AT=2 build/reelcut demo.reel` saves a screenshot of the window after a few seconds and quits.
