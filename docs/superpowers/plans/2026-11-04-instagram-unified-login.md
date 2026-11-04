# Instagram Unified Login Implementation Plan

**Goal:** Accept the user's Unified Login token while preserving Instagram Login connections.

**Architecture:** Extend InstagramApi's existing Qt network adapter with a persisted token route and assigned-account discovery. Keep account validation separate from saved connection state.

**Tech stack:** C++17, Qt 6, QtTest, localhost HTTP fixtures.

**Spec:** ../specs/2026-11-04-instagram-unified-login-design.md

## Task: token compatibility

- [x] Extend tests/reelcut_tests.cpp with a Unified Login fixture. Cover connection, exact ID, publishing, reload, renewal skip, failed reconnect, empty and multiple account lists.
- [x] Run the new tests and observe failure from missing support.
- [x] Modify src/instagramapi.{h,cpp}: select API route, validate the assigned account, persist route, and avoid Instagram-only renewal for Unified Login.
- [x] Update src/PostDialog.qml instructions for both setups and accurate renewal wording.
- [x] Run focused integration tests, bin/test, and bin/build. Review only this task's changes against preserved originals. Leave the open editor running.

## Review focus

Preserve an existing connection after rejected input; never mistake /me's system-user ID for an Instagram ID; reject ambiguous accounts; preserve 17-digit IDs; keep legacy accounts working after reload.

## Verification record

- RED: new Unified Login cases failed because /me was used; the legacy integration case passed. Local networking required execution outside the network sandbox.
- GREEN: final focused checks passed (7 including setup/cleanup); both legacy and Unified routes connect and publish using a localhost API and dummy tokens. Reconnection rejection, exact IDs, saved account reload, empty/multiple account lists and skipped legacy renewal are covered.
- bin/build passed; built binary: /home/jp/reelcut/build/reelcut. Help smoke check passed.
- Full suite: 76 passed, 1 failed: previewItemsDragAndResize (overlay drag x > 0.6), in unchanged preview behavior. Existing Qt multimedia/thumbnail warnings are present. No preview changes made for this task.
- Scoped independent review found no blocking correctness/security issues. Added rejected-EAA reconnection coverage following review. Deferred coverage: actual production host selection (local tests override the host), pagination and malformed account responses. Production route was inspected against Meta's current documentation.
- Ruling: derive the persisted route from the saved EAA prefix rather than adding a redundant config field. Other Facebook token types are not supported by this Unified Login discovery flow.
- Ruling: work in place to preserve the substantial existing uncommitted/untracked app work. Original touched files were copied to /tmp for scoped review; no commits, reset, or system installation.
- Desktop control unavailable. Do not close or relaunch the user's editor. User must open the built binary and retry the token; real-token validation remains pending.
