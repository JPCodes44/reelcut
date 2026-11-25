# Songs page (ACE-Step on Modal)

The **Songs** button in the top bar opens a page that makes songs with
[ACE-Step 1.5](https://github.com/ace-step/ACE-Step-1.5) (Apache-2.0, so songs
can be used commercially) running on a Modal serverless GPU. The **Modal** meter
next to the Higgsfield meter shows this month's estimated spend.

## Setup (once)

```bash
uv tool install modal
modal setup                                   # log in
modal run modal/acestep_app.py::download      # ~10 GB of models into a Modal volume
modal deploy modal/acestep_app.py             # prints the api URL
```

Then create a proxy auth token at modal.com → Settings → Proxy Auth Tokens,
click the **Modal** meter in Reelcut, paste the URL, token ID (`wk-…`) and
secret (`ws-…`), press **Save**, then **Test**.

The settings are saved in `~/.config/reelcut/modal.json`, readable only by you.

## Using it

Every box is optional. Leave them all empty for a random song.

| Box | What it does |
|---|---|
| What it's about / Style | The song description. Style chips add to the style. |
| Lyrics | Words to sing. Empty: ACE-Step writes them. **Instrumental**: no vocals. **Load .txt** reads a file. |
| Length | 10–240 s. Empty: ACE-Step picks. |
| Versions | 1–4 takes per request. |
| Reference song | The new song follows its sound (audio or video file). |
| Audio to remix | Cover it in a new style, extend it, or repaint a time range. |
| Images | Claude Code (`claude -p`) describes their mood; the text is added to the style and can be edited. |

Songs are saved in `~/Videos/Reelcut songs/<date>-<name>/` as `take-N.mp3` with
`request.json` (what was asked, what was sent, the GPU, time and estimated cost).
**Use in reel** sets a take as the open reel's music.

The Reelcut MCP can drive it too: `app_songs_generate`, `app_songs_reload`,
`app_ui_navigate` with `page: "songs"`.

## Speed and cost (measured 2026-11-25)

| | Time | Est. cost |
|---|---|---|
| First song after the GPU has slept (cold start, words written by ACE-Step, 30 s song) | ~115 s | ~$0.045 |
| Next song while the GPU is awake (60 s instrumental) | ~11 s | ~$0.004 |
| GPU staying awake after the last song (120 s) | — | ~$0.04 |

So a session of a few songs costs roughly $0.10–0.15, and the $30 free monthly
credit covers a few hundred sessions.

The GPU is an L4 when one is free, otherwise an A10 or L40S. Reelcut asks the
server which one it got (`/reelcut/gpu`) and prices the job at that GPU's list
rate plus 4 CPU cores and 16 GiB. An unknown GPU is priced as an L40S.

These are estimates of Reelcut's own use. Check modal.com/usage for the bill.

## How it fits together

- `modal/acestep_app.py`: the Modal app. ACE-Step's own REST server behind proxy
  auth, at most one container, asleep after 120 s idle. A small wrapper adds
  `/reelcut/gpu`, keeps a requested length in ACE-Step's write-the-words mode,
  and links ACE-Step's checkpoint folder to the volume (without that it
  re-downloads 4.5 GB on every cold start).
- `src/songrequest.*`: the page's request → ACE-Step form fields.
- `src/songgen.*`: wakes the GPU with a GET (Modal turns a request that waits
  over 150 s into a redirect, which only a GET can follow), sends the job, polls,
  downloads takes, writes `request.json`.
- `src/modalusage.*`: the monthly ledger (`~/.config/reelcut/modal-usage.json`)
  and `modal-status.json` for scripts.
- `src/SongsPage.qml`, `src/ModalMeter.qml`, `src/songs.qrc`.
- Tests: `bin/test-songs`.
