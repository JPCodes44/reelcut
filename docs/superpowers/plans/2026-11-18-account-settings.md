# Account Settings Implementation Plan

> Execute inline using the existing publishers. Preserve user edits.

**Goal:** Separate all-platform Settings with accurate credential lifetimes.
**Architecture:** Accounts QObject provides non-secret status and connection operations over existing native publishers; SettingsPage renders it. Post uses the same wrapper; Stats links to the relevant account.
**Tech stack:** C++17, Qt6 Network/Quick Controls, QtTest.
**Spec:** docs/superpowers/specs/2026-11-18-account-settings-design.md

## Global constraints
- Use the social checkout launched by the desktop app; no duplicate credentials.
- Unknown metadata remains Unknown; no embedded Meta app secrets.
- Preserve unrelated edits and working credentials on validation failure.

## Review focus
- Persisted state survives startup signal ordering.
- Pending requests cannot reconnect after disconnect.
- Page-token fallback and pagination cannot connect the wrong Page.
- Invalid replacement metadata preserves the current account.
- Transient YouTube failures preserve prior validity.

## Tasks
- [x] Identify the actual launched checkout and snapshot its files; restore the initial checkout prototype.
- [x] Add Accounts status model and Meta preflight validation, native Page token metadata, generation guards and credential-safe errors.
- [x] Expose YouTube access/refresh lifetimes from OAuth responses and persist reported dates.
- [x] Add Settings toolbar entry, account controls, expiry display and scheduling tab; link Post and Stats.
- [x] Add regression tests for publisher sharing, persistence, invalid replacement, Page tokens/pagination, YouTube expiry/transient failures and opening Settings without clips.
- [x] Run full social-version suite, inspect screenshot and review diff.
- [x] Apply the validated files to the launcher checkout and rebuild its application binary.

## Validation ledger
- Original checkout prototype: account tests passed; original source diff restored byte-for-byte.
- Social integration: focused test initially found one existing synchronous Page-selection assertion; updated to wait for metadata validation. Review findings reproduced by new Page-token and transient YouTube tests, then fixed.
- Full suite: in progress. A preview drag/resize failure also reproduces in the pre-Settings original checkout; compare actual social baseline if needed.

- Social suite result: 111 passed, 1 failed: previewItemsDragAndResize. The same assertion fails in the original root baseline and in the existing unmodified social test binary. All account/settings regressions pass. Screenshot /tmp/reelcut-social-settings.png inspected.

- Applied 22 files after verifying original hashes; application rebuilt successfully in /home/jp/reelcut-social/build. Final targeted regression run: 11 passed, 0 failed. Source matches staging; original /home/jp/reelcut diff remains byte-for-byte unchanged.
