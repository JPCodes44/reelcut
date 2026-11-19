# Daily Schedule Page Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Add a separate daily Schedule page for ten staggered video slots per platform and explicit scheduling through the existing Post dialog.

**Architecture:** Keep pure date/slot/record functions in ScheduleLogic.js, persistence and calendar records in Backend, and presentation in SchedulePage.qml. Join confirmed Stats reels to scheduling records by platform/link; never infer publishing success from time alone. Main.qml routes slot clicks into PostDialog.openScheduled(platform, publishAt).

**Tech Stack:** C++17, Qt Quick/Controls/Layouts, QML JavaScript, QtTest; no new dependencies.

**Spec:** docs/superpowers/specs/2026-11-18-daily-schedule-design.md

## Global Constraints
- Ten slots per platform; initial starts 08:00, 08:30, 09:00, each 90 minutes apart.
- Separate 60/90/120-minute spacing and start preferences per platform; final slot before midnight.
- Dates and time labels use the computer's local timezone.
- No export/upload/publish from opening an empty slot.
- Preserve the pending-only scheduledPosts interface and unrelated worktree changes.

## Review Focus
- Midnight and daylight-saving boundaries must not put posts on the wrong day or silently shift a slot.
- Past direct schedules remain visible as Awaiting confirmation, never falsely Published.
- Missing thumbnails, offline accounts and no current reel must leave the page readable and safe.
- Closely spaced and off-grid actual posts must remain accessible, not disappear behind empty slots.
- Switching pages and closing a popup must not activate hidden-editor shortcuts or reuse a stale posting time.

### Task 1: Schedule model and persistence
**Files:** src/ScheduleLogic.js; src/backend.h; src/backend.cpp; src/resources.qrc; tests/reelcut_tests.cpp.
**Interfaces:** ScheduleLogic exports dayStart(value), addDays(value, offset), slots(day, startMinute, spacingMinutes), dayPosts(day, scheduled, published, now), layoutCards(cards), and platformSummary(platform, cards, targetSlots). Backend exposes schedulePreferences (QVariantMap, writable, schedulePreferencesChanged signal) and calendarPosts (QVariantList, schedulerChanged signal). Existing scheduledPosts remains pending-only.
- [x] Add meaningful QtTest/QML-script tests for ten 90-minute slots, date boundaries, invalid/overflow settings and record status/identity, then run them and observe failure.
- [x] Implement pure model functions and preference persistence using Net::stateFile(stateDir, "schedule-plan.json"). Retain at most 90 days of direct scheduled records on load/save for calendar history while preserving scheduledPosts filtering.
- [x] Test actual posts outside slots, duplicate published links, no caption-based merges, missing fields and cancelled/failed counts. Assert expired direct records are awaiting confirmation unless matched to Stats.
- [x] Run focused model/backend tests to green.

### Task 2: Separate Schedule page
**Files:** src/SchedulePage.qml; src/ScheduleLogic.js; src/resources.qrc; tests/reelcut_tests.cpp.
**Interfaces:** SchedulePage has closeRequested() and scheduleRequested(string platform, double publishAt), consumes Backend preferences/calendarPosts and optional Stats.reels, and exposes selectedDate for navigation tests.
- [x] Add UI tests for date navigation, 30 empty slots, mixed platform cards, editable cadence validation, no-current-reel guidance and narrow layout.
- [x] Implement platform headers, progress counts, preference editor, shared scrollable daily axis, thumbnails/fallbacks and status cards; render close/off-grid events without hiding them.
- [x] Read scheduled records on page opening and offer explicit Refresh for existing scheduler/Stats sources. Settings do not alter existing scheduled records.
- [x] Run UI/model tests to green and capture a representative rendered page.

### Task 3: Navigation and explicit scheduling handoff
**Files:** src/Main.qml; src/PostDialog.qml; tests/reelcut_tests.cpp.
**Interfaces:** PostDialog.openScheduled(platform: string, publishAt: double) opens in write stage, selects exactly one platform, sets scheduleLater true and fills postWhen with yyyy-MM-dd HH:mm. Normal openFor() clears any calendar-specific preset.
- [x] Add regression tests for Schedule/Stats mutual exclusion, Escape/hidden-editor shortcuts, exact platform/time prefill and zero network/export side effects on slot opening.
- [x] Wire the Schedule top-bar button and slot action into the existing Post dialog. Respect backend.checkWhen and the existing scheduler/account configuration flows.
- [x] Run existing scheduling, cross-posting and Stats regressions plus the new tests. Build the app, inspect rendered page at standard/minimum width, review the final diff and report limitations.

## Execution and commits
The worktree already contains unrelated uncommitted work. Keep this feature's changes reviewable without committing or staging unrelated files. This plan is ready for native execution after user review; no generation, account changes, posting or deployment is authorized by this plan.

## Validation outcome
Implemented in the existing social-platforms worktree, with no commits, account changes or real posts. App build passes. Focused Schedule/publishing/Stats checks: 15 passed, 0 failed. Additional Santiago midnight-DST regression passes. Full suite: 107 passed, 1 existing preview drag failure; reproduced with Schedule UI/navigation removed in a temporary copy. Final fresh-context review findings were fixed and covered by regression tests. Rendered at 1360px and 980px, including timing settings and mixed status cards.
