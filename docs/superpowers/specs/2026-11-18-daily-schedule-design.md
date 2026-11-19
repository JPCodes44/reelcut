# Daily Schedule page

Approved in conversation on 2026-11-18.

## Intent
The user generates videos manually and needs a separate page to see and fill a staggered daily publishing schedule: ten distinct videos per platform per day, on Instagram, Facebook and YouTube, with one-to-two-hour spacing. No AI generation or autonomous posting is part of this feature.

## Approach
Use a shared daily timeline with three platform columns. A card grid was considered but does not make gaps and simultaneous times as clear. Reuse the existing Post dialog and publishing backends; a second posting implementation would duplicate validation and risk publishing without the user's explicit action.

## Behavior
- A Schedule top-bar button opens a page covering the editor, like Stats. Stats and Schedule are mutually exclusive; Escape closes the page without editing the hidden timeline.
- Previous day, Today, Next day, a readable date and the computer's local timezone are visible.
- Each platform has ten target slots. Initial start times are 08:00 Instagram, 08:30 Facebook and 09:00 YouTube, with 90-minute spacing. Users can edit start time and spacing (60, 90 or 120 minutes) separately per platform. These preferences persist.
- Invalid settings that put the final slot after midnight cannot be applied. Slot calculations follow local calendar dates, including daylight-saving transitions; an unavailable wall-clock time is visibly disabled rather than silently moved.
- Display actual scheduled posts and known published reels at their times, alongside unfilled target slots. Cards show time, platform, title/caption, available thumbnail or platform fallback, and status. Posts outside target slots remain visible. Per-platform summaries show scheduled/published coverage against ten target slots, separately from failures.
- Empty future slots open the existing Post dialog, prefilled with that one platform and exact local date/time. Opening a slot never exports, uploads or publishes. With no current reel, explain that the user must open or prepare one first. Past slots cannot schedule.
- Scheduled, publishing, published, failed and cancelled are distinguished. A direct Facebook/YouTube scheduled record whose time has passed is Awaiting confirmation until a matching published reel is observed; do not infer successful publication from elapsed time. Deduplicate records when a platform post link matches Stats data. Do not guess matches based only on captions or times.
- Settings edits change empty planning slots only, never the times of already scheduled posts. External platform schedules can be opened on-platform; retain the existing scheduler's status/error handling.
- Page layout remains usable at Reelcut's 980px minimum width, with a shared scrollable time axis and readable platform headers.

## Constraints and validation
Keep the existing Qt Quick/C++ stack and theme, without new dependencies. Preserve current changes in the social-platforms worktree. Tests cover date boundaries, slot spacing, invalid settings, mixed records, uncertain publication status, deduplication and Post dialog prefill. Run the existing scheduling regressions and app build, and render the page with representative data.

## Out of scope
AI generation, conversion attribution, experiment allocation, automatic batch scheduling, arbitrary file intake, dragging already scheduled posts to new times, and changing the connected accounts.
