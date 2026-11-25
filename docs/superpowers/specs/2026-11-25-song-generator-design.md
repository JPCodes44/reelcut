# Song generator (ACE-Step on Modal)

**Date:** 2026-11-25
**Status:** Draft for review

## Purpose

A Songs page in Reelcut that makes songs with ACE-Step 1.5 running on Modal serverless GPUs,
from an optional prompt, style, lyrics and attachments. A top-bar meter shows Modal spend
against the free monthly credit. Songs land in a folder and can be dropped straight into the
current reel as its music.

ACE-Step 1.5 is Apache-2.0, so generated songs can be used commercially.

## User interface

### Songs page

Opened by a new checkable **Songs** button in the top bar (same pattern as Schedule and Workflow,
`win.songsVisible`). Two columns.

**Left: request form. Every field is optional.**

| Field | Notes |
|---|---|
| Prompt | What the song is about. Maps to ACE-Step `caption` together with Style. |
| Style | Free text plus quick chips (pop, hip-hop, lo-fi, EDM, rock, acoustic, cinematic, comedic). Chips append to the text. |
| Lyrics | Multi-line box. **Load .txt** fills it from a file. **Write lyrics** asks ACE-Step's LM to write them from the prompt and style (`/format_input`) and puts the result in the box for editing. **Instrumental** toggle sends no lyrics and marks the request instrumental. |
| Length | Seconds, 10–240. Empty means ACE-Step picks. |
| Versions | 1–4 songs per request (`batch_size`), default 2. |
| Reference song | One audio/video file. ACE-Step `reference_audio` (style transfer). |
| Audio to remix | One audio/video file plus a mode: **Cover** (new style), **Extend** (`complete`), **Repaint** (with start/end seconds). ACE-Step `src_audio` + `task_type`. |
| Images | Up to 6 images. Before generating, Reelcut runs `claude -p` to describe their mood, genre, tempo and instruments in one short paragraph. The description appears in an editable "From images" box and is appended to the style. |

If every field is empty, Generate first calls ACE-Step `/create_random_sample` and sends the
returned parameters to `/release_task`, so you get a random song.

Video files are converted to audio with ffmpeg before upload.

**Right: songs list.** Newest first, from this session and earlier ones (read back from the songs
folder). Each card shows the name, a play/pause button, a waveform (existing waveform provider),
duration, generation time and estimated cost, plus buttons:

- **Use in reel**: sets the song as the open project's music (existing `setMusicValue` path).
- **Show file**: opens the folder.
- **Remix this**: loads the song into "Audio to remix".

A running request shows a card with its stage: *Describing images*, *Starting GPU (first time
~30–60 s)*, *Generating*, *Downloading*, with elapsed time. Failed requests show the error on
the card.

### Modal meter (top bar)

Next to the Higgsfield `CreditsMeter`. Shows `Modal $4.12 / $30` and a thin bar.

- Default limit is the $30 free monthly credit. Editable in Settings.
- Orange at 80%, red at 100%.
- At or past 100%, Generate shows a confirm dialog before sending.
- Tooltip: this month's song count, GPU seconds, idle-tail seconds, and "estimates at Modal list
  prices; check modal.com/usage for the bill".
- Resets on the first of each calendar month.

### Settings: Modal section

Endpoint URL, token ID and token secret (secret masked), monthly limit, and a **Test** button that
calls the health endpoint. Stored in `~/.config/reelcut/modal.json` (mode 600).

## Architecture

```
Reelcut (C++/QML)                                   Modal app "reelcut-acestep"
  SongsPage.qml ── SongGenerator ── HTTPS ───────►  web server (ACE-Step API server)
                     │  Modal-Key / Modal-Secret      L4 GPU, 1 container max,
                     │  headers (proxy auth)          scaledown 120 s, timeout 600 s
                     ├─ claude -p (images)            Volume: checkpoints (~9 GB)
                     ├─ ffmpeg (video → audio)
                     └─ ModalUsage ── modal-usage.json / modal-status.json
  ModalMeter.qml ── ModalUsage
```

### Modal app: `modal/acestep_app.py`

- Image: CUDA base + Python, clone ACE-Step-1.5 at a pinned commit, install its requirements.
- Volume `reelcut-acestep-checkpoints` mounted as the checkpoints folder. First start downloads
  the turbo DiT, the 1.7B LM, the VAE and the text embedder, then commits the volume.
- `@modal.web_server` runs ACE-Step's API server on its port with `requires_proxy_auth=True`.
- `gpu="L4"`, `max_containers=1`, `scaledown_window=120`, `timeout=600`.
- Deploy: `modal deploy modal/acestep_app.py`. Proxy auth token is created in the Modal
  dashboard (Settings → Proxy Auth Tokens).

### `src/songgen.{h,cpp}`: `SongGenerator` (QML context property `songs`)

- `Q_INVOKABLE generate(QVariantMap request)`: builds the ACE-Step multipart request from the
  filled-in fields only, posts `/release_task`, polls `/query_result` every 2 s, downloads each
  audio URL, writes `~/Videos/Reelcut songs/<yyyy-MM-dd>-<slug>/take-N.mp3` and `request.json`
  (inputs, ACE-Step params, task id, timings, estimated cost).
- `Q_INVOKABLE describeImages(QStringList paths)`: runs `claude -p` with the image paths and a
  fixed instruction; emits the text or an error.
- `Q_INVOKABLE writeLyrics(QVariantMap request)`: calls `/format_input`.
- `Q_INVOKABLE health()`: used by Settings Test and before the first request.
- Properties: `configured`, `busy`, `stage`, `songs` (list of maps), `lastError`.
- One request at a time; Generate is disabled while busy.
- Pure request-building is a free function, `buildAceStepForm(QVariantMap) -> fields`,
  so it can be unit-tested.

### `src/modalusage.{h,cpp}`: `ModalUsage` (context property `modalUsage`)

- Ledger `~/.config/reelcut/modal-usage.json`: `{month, limit, entries:[{at, kind, seconds, dollars}]}`.
- Cost per second (one constant table, L4 list prices): GPU $0.000222 + 4 CPU cores ×
  $0.0000131 + 16 GiB × $0.00000222 ≈ $0.000310/s.
- Billed seconds recorded per request = time from first byte sent to the last response
  (covers cold start and generation). After a request finishes, a 120 s timer starts; if no new
  request arrives before it ends, the 120 s idle tail is recorded as its own entry. A new
  request within the window records only the elapsed part of the tail.
- If Reelcut quits during the tail, the remaining tail is recorded on quit.
- Properties: `spent`, `limit`, `fraction`, `level` (ok/warn/over), `songCount`.
- Writes `~/.config/reelcut/modal-status.json` (`spentDollars`, `limitDollars`, `overBudget`) on
  every change, like the Higgsfield meter.

### Automation / MCP

Register `songs.generate`, `songs.list` and `songs.use_in_reel` in the automation registry so the
Reelcut MCP can drive the page, following the existing registry pattern.

### Shared-file changes (additions only)

`Main.qml` (button, page, `songsVisible`), `main.cpp` (two context properties),
`reelcut.pro`, `resources.qrc`, `SettingsPage.qml` (Modal section), `automationregistry.cpp`,
`tests/reelcut_tests.cpp`.

## Error handling

| Situation | Behaviour |
|---|---|
| No endpoint or token | Generate disabled; hint links to Settings → Modal. |
| 401/403 | "Modal token rejected" with link to Settings. |
| 404 / app not deployed | "Modal app not deployed: run modal deploy modal/acestep_app.py". |
| Task status 2 (failed) | Error text on the card; entry recorded with its billed seconds. |
| Poll exceeds 10 min | Card fails with "timed out"; billed seconds recorded. |
| Network drop mid-poll | Retry the poll 3 times, then fail the card. |
| `claude` missing or fails | Warning on the form; generation continues without image text. |
| ffmpeg conversion fails | Card fails with the ffmpeg error before anything is sent. |
| Over the monthly limit | Confirm dialog; nothing is blocked after confirming. |

## Testing

Unit tests in `tests/reelcut_tests.cpp` (no network):

- `buildAceStepForm`: empty request → random-sample request; each field alone; instrumental drops
  lyrics; cover/extend/repaint map to the right `task_type` and fields; length bounds.
- `ModalUsage`: cost math, idle-tail accounting (finished, interrupted by a new request, on quit),
  month rollover, level thresholds at 80% and 100%.
- `SongGenerator` against a local fake server (`QTcpServer`) serving `/release_task`,
  `/query_result` and an audio file: files and `request.json` are written, failures surface.

Manual: deploy, generate one 60 s song, record real wall time and estimated cost in the README
section for this feature.

## Development setup

Work happens in a git worktree on branch `song-generator`, so the other sessions' uncommitted
changes on `main` are never touched or committed.

## Out of scope

Training or LoRAs, stems/`extract`, `lego`, more than one container, reading Modal's real
billing API, a local-GPU mode, editing songs on the timeline beyond setting them as music.
