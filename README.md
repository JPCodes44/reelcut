# Reelcut

Make vertical reels for Instagram and TikTok on Omarchy: cut your clips, caption them automatically, give them a look, add music, and export an MP4 that's ready to post.

![Reelcut editing a reel](docs/screenshot.png)

## What it does

- **Photos and slideshows**: drop in photos (JPG, PNG, WebP, HEIC) and they become slides with a duration and an optional slow zoom or pan. Export the slideshow as a video, or as a **photo set** for TikTok photo posts: one 1080×1920 JPG per slide with the text and look drawn in, numbered in order. Post the set in TikTok's photo mode and pick the sound in the app (TikTok's library sounds can only be added there).
- **Cut**: drop in videos, split at the playhead, trim with the handles, reorder, change speed from 0.25× to 4×.
- **Frame**: every clip fills the 9:16 frame; zoom and pan landscape footage to pick what shows.
- **Cut silences**: one click removes the pauses from a talking clip.
- **Captions**: transcribed on your computer with whisper.cpp, word by word, in four styles (highlight the spoken word, pop one word at a time, bold, or on a box). Click any word to fix it.
- **Text**: hook titles that pop, fade or slide in.
- **Look**: exposure, contrast, saturation and temperature, six presets, and any `.cube` LUT (including ones from DaVinci Resolve).
- **Music**: a music track that ducks under speech, and loudness leveled to -14 LUFS.
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
| Delete | Delete the selected clip |
| Alt+← / Alt+→ | Move the selected clip |
| I | Add clips |
| T | Add text at the playhead |
| G | Show or hide safe zones |
| Ctrl+Z / Ctrl+Shift+Z | Undo / redo |
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
