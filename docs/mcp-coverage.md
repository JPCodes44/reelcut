# Reelcut MCP coverage

Audited 2026-11-18. `get_app_capabilities` returns the current inventory; [mcp-capabilities.json](mcp-capabilities.json) is the checked-in snapshot.

## Measured functional coverage

100% of the audited application API: 106 public invokable operations (98 directly exposed and 8 mapped to deterministic equivalents), 78 declared properties, and 26 explicit UI actions. The audit reports no unsupported entries. This measures functional API coverage, not C++ line coverage or proof of every possible workflow. QML controls were reviewed separately against the UI facade.

| Component | Direct operations | Equivalent operations | Properties |
| --- | ---: | ---: | ---: |
| Editor, layers, effects, captions, export, posting and calendar backend | 77 | 7 | 45 |
| Account connections and token status | 6 | 0 | 3 |
| Stats and history | 3 | 0 | 6 |
| Downloads and file clipboard | 7 | 1 | 4 |
| Higgsfield credits and budget | 5 | 0 | 16 |
| Theme | 0 | 0 | 3 |
| Language | 0 | 0 | 1 |

The existing 28 saved-project tools remain available. Eight common live tools plus 98 typed operations and 26 UI actions bring the advertised total to 160 tools.

## Connecting and using the live app

Keep the existing `reelcut mcp` configuration. Restart or reconnect the MCP client after updating Reelcut so it discovers the new tools. Restart an older running Reelcut window to load the local bridge.

1. Call `get_app_capabilities` to inspect coverage and `list_app_instances` to find windows. `launch_app` opens a GUI when a desktop display is available.
2. Call `get_app_state` for all components or a single `backend`, `accounts`, `stats`, `downloads`, `credits`, `theme`, `language` or `ui` component.
3. Use typed `app_backend_*`, `app_accounts_*`, `app_stats_*`, `app_downloads_*`, `app_credits_*` and `app_ui_*` tools. Supply the named arguments advertised in each schema; overloaded methods use the full argument form. `set_app_property` changes declared writable properties only.
4. Poll `get_app_events(after)` and state for asynchronous completion. `accepted` means the call returned, not that export, sign-in, scheduling, publishing or download succeeded. A timeout must not be blindly retried.

When several windows are running, pass the chosen `instance` ID. Live tools require a running updated GUI. Saved-project tools work without one. Prefer live tools while editing unsaved GUI state; saved-project tools edit the file and the GUI reloads it.

## UI functions

The facade covers page navigation, Settings Accounts/Scheduling tabs, playback and seeking, selection and deletion, inspector/view options, timeline zoom/scroll and music row height, Stats filters/detail, calendar dates/slots/details/Timing, post draft fields and explicit submission/back, preview caption editor, dialog dismissal, unsaved-change confirmation and quit. `get_app_screenshot` returns the window with credential inputs masked. `sample_preview_color` captures the preview picture, matching the native eyedropper rather than sampling window controls or modal overlays.

Posting is an explicit action. Reading state, modifying a draft and validating are separate from `app_ui_post_submit` or backend publishing operations. TikTok uses Reelcut's existing guided browser/manual flow; MCP does not add an unsupported direct TikTok API.

## Native gesture equivalents

- Add media/music/LUT: `app_backend_select_files(purpose, paths)` with `media`, `music` or `lut`.
- Open/save/export pickers: `app_backend_open_file(path)` or `app_backend_select_files` with `open_project`, `save_project` or `export`.
- Eyedropper: `sample_preview_color(x,y)` with normalized coordinates.
- Download drag: read local paths from download state, then add the file to the appropriate track/layer. `app_downloads_copy_file` produces the native file clipboard payload.
- Reorder/trim/resize/drag/keyboard editing: equivalent typed backend mutations plus UI selection/seeking; physical gestures are not required.

Replacing a dirty project requires saving first or an explicit `discard_unsaved=true`. OAuth consent, provider passwords and CAPTCHA entry remain human browser steps. Token lifetimes and permissions remain controlled by each platform.

## Credential and transport boundary

The GUI accepts same-user local Unix-socket calls only, with per-window identity, strict typed arguments, bounded request/response sizes, timeouts and a bounded event log. No arbitrary QML evaluation, QObject traversal, private slots or credential getter methods are exposed. Credential connection inputs are write-only; known tokens/secrets are masked from textual results/events and credential fields are painted over before screenshot encoding.

## Verification

The integration suite exercises real Backend mutations over local sockets, multiple-window selection, UI navigation/drafts/Timing, argument rejection, dirty-project protection, credential redaction, screenshot decoding and asynchronous fake-service events. `automationInventoryCoversFunctionalApis` fails when a public method/property lacks a supported schema or documented equivalent. UI additions still require a corresponding facade/audit review.

The existing `previewItemsDragAndResize` offscreen test failure was reproduced before these changes and is tracked separately from MCP coverage.

Final verification: full Qt suite **119 passed, 1 failed** (the previously reproduced `previewItemsDragAndResize` assertion). Rebuilt launcher executable successfully; its stdio server advertised **160 unique tools** and 100% audited coverage. An isolated rebuilt GUI was controlled through stdio MCP for instance discovery, all eight state components, Settings navigation and screenshot capture. No real account connection or publishing was performed.
