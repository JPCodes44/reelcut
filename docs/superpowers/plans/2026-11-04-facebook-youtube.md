# Facebook Reels and YouTube Shorts Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Preview, publish and schedule reels on Facebook (Pages) and YouTube (Shorts) alongside Instagram and TikTok, including one-export cross-posting.

**Architecture:** A platform table in `src/posting.cpp` replaces every two-way instagram/tiktok choice. A small `Publisher` base class is shared by `InstagramApi`, a new `FacebookApi` (Reels Publishing API) and a new `YouTubeApi` (loopback OAuth + resumable upload). `Backend::postTo` exports once and runs the selected publishers in turn; `PostDialog.qml` gets multi-select.

**Tech Stack:** C++17, Qt 6 (Core, Network, Quick), QML, QtTest; qmake6 via `bin/build` and `bin/test`.

Spec: `docs/superpowers/specs/2026-11-04-facebook-youtube-design.md`.

## Global Constraints

- Platform ids and order: `instagram`, `facebook`, `youtube`, `tiktok`; unknown names fall back to `instagram`.
- Safe zones (left, top, right, bottom px of 1080×1920): instagram 40,220,950,1500 · facebook 65,270,960,1250 · youtube 48,290,890,1400 · tiktok 40,160,930,1440.
- Graph API version v26.0; Facebook scheduling 10 minutes to 29 days ahead; Reels 3–90 s; Shorts ≤ 180 s.
- YouTube title 1–100 chars, no `<` or `>` in title or description; `#Shorts` added to the description once.
- Tokens only in mode-600 files under `~/.config/reelcut` (or `stateDir` in tests); never in fixtures.
- Every API base address is overridable for tests (`graphBase`, `uploadBase`, `authBase`, `tokenUrl`, `apiBase`).
- Baseline: `bin/test` passes 80/80 before any change.

---

### Task 1: Platform table drives safe zones, caption home and platform names

**Files:** Modify `src/posting.h`, `src/posting.cpp`, `src/project.h`, `src/project.cpp`, `src/backend.h`, `src/backend.cpp`, `src/Preview.qml`, `src/Main.qml`, `src/Inspector.qml`, `src/mcpserver.cpp`; Test `tests/reelcut_tests.cpp` (`platformTableCoversFourApps`).

**Interfaces — Produces:**
- `struct Posting::SafeZone { double left, top, right, bottom; };`
- `Posting::Target` gains `SafeZone safe; int minSeconds, maxSeconds;` (0 = no limit)
- `Posting::Target::toMap()` → QVariantMap `{id,label,maxChars,maxHashtags,minSeconds,maxSeconds,safeLeft,safeTop,safeRight,safeBottom}`
- `QStringList Posting::ids()`; `QString Posting::normalize(const QString &)`
- Backend `Q_PROPERTY(QVariantList platforms CONSTANT)`, `Q_PROPERTY(QVariantMap platformInfo NOTIFY projectChanged)`

- [ ] Test: for each id, `Project::setPlatform(id)` keeps it, `captionHome(id)` = (mid of left/right, bottom−190), `fromJson` keeps `facebook`/`youtube`, unknown → instagram; `Posting::ids()` order.
- [ ] Run, expect compile failure. Implement. Run `bin/test`, expect pass. Commit.

### Task 2: Facebook and YouTube chrome in the preview

**Files:** Modify `src/SocialPreview.qml` (`platform: string`), `src/SocialIcon.qml` (thumbUp, thumbDown, remix, bell, menu, video, dislike), `src/Preview.qml`; Test: extend `socialPreviewEditingAndLayout` to all four ids and `platform` property.

- [ ] Change the test to read `platform`, loop all ids. Fail. Implement layouts. Pass. Commit.

### Task 3: Publisher base; Instagram derives from it

**Files:** Create `src/publisher.h`; modify `src/instagramapi.h/.cpp`, `reelcut.pro`, `tests/reelcut_tests.pro`.

**Produces:** `struct PostRequest { QString file, text, title; QDateTime when; }`; `class Publisher : QObject` with pure virtuals `platform()`, `connected()`, `account()`, `busy()`, `publish(const PostRequest &)`; signals `accountChanged()`, `progress(QString,double)`, `published(bool,QString,QUrl)`. `InstagramApi::publish(const PostRequest &)` calls `publish(file, text)`.

- [ ] Existing Instagram tests are the regression check. Implement. `bin/test` passes. Commit.

### Task 4: FacebookApi

**Files:** Create `src/facebookapi.h/.cpp`; add to both .pro files; Test `FakeFacebook` + `facebookConnectsAndPublishes`, `facebookPagePickingAndErrors`.

**Produces:** `FacebookApi : Publisher` with `connectToken(QString)`, `choosePage(QString id)`, `disconnect()`, `pageId()`, `accountFile()`; signals `connectFinished(bool,QString)`, `pagesFound(QVariantList)`; members `graphBase`, `stateDir`, `pollIntervalMs`.

- [ ] Tests: one page → connected, file mode 600; three pages → pagesFound then choosePage; none → error, old connection kept; publish now → start, rupload with `OAuth <page token>` + file_size, finish `PUBLISHED`, status polled, permalink link; scheduled → `SCHEDULED` + unix time; status error → fails with message.
- [ ] Fail, implement, pass, commit.

### Task 5: YouTubeApi

**Files:** Create `src/youtubeapi.h/.cpp`; .pro files; Test `FakeGoogle` + `youtubeSignsInAndUploads`.

**Produces:** `YouTubeApi : Publisher` with `connectClient(id, secret)`, `disconnect()`, `accountFile()`, `channelTitle()`; `std::function<void(const QUrl &)> openUrl`; signal `connectFinished(bool,QString)`; members `authBase`, `tokenUrl`, `apiBase`, `uploadBase`, `stateDir`.

- [ ] Tests: connect opens an auth URL with state, code_challenge S256, offline; test follows redirect_uri with code+state; token exchange carries verifier; channel saved, mode 600; wrong state fails. Publish now → resumable POST JSON public + `#Shorts`, PUT bytes, link `https://youtube.com/shorts/<id>`; scheduled → private + publishAt; expired access token refreshed.
- [ ] Fail, implement, pass, commit.

### Task 6: Backend cross-posting

**Files:** Modify `src/backend.h/.cpp`; Test `crossPostsToEveryCheckedPlatform`, `checkPostsMergesLimits`.

**Produces:** `Q_INVOKABLE QVariantMap checkPosts(QStringList platforms, caption, hashtags, title)`; `Q_INVOKABLE void postTo(QStringList platforms, caption, hashtags, title, double publishAtMs)`; `Q_INVOKABLE QVariantMap checkWhen(QStringList platforms, double ms)`; `facebook()`, `youtube()` accessors; Q_PROPERTYs `facebookPage`, `youtubeChannel`; invokables `connectFacebook(token)`, `connectFacebookWithInstagram()`, `chooseFacebookPage(id)`, `disconnectFacebook()`, `connectYouTube(id, secret)`, `disconnectYouTube()`; signals `platformPosted(QString,bool,QString,QUrl)`, `facebookPages(QVariantList)`, `accountsChanged()`; direct scheduled posts merged into `scheduledPosts` with `platform` and `link`.

- [ ] Tests: limits union (YouTube title empty / `<`, Facebook clip under 3 s); post to instagram+facebook+youtube against fakes → one export, three `platformPosted`, `postPublished(true)`; facebook failing still posts youtube and ends `postPublished(false)`; scheduling facebook 5 minutes ahead refused before export.
- [ ] Fail, implement, pass, commit.

### Task 7: Post dialog multi-select

**Files:** Modify `src/PostDialog.qml`, `src/Main.qml`; Test: existing dialog tests keep passing; new `postDialogCrossPosts`.

- [ ] Test drives toggles postTo-instagram/facebook/youtube, sets postTitle, Next → results list `postResults` with three rows; TikTok toggle clears others.
- [ ] Fail, implement, pass, commit.

### Task 8: Docs and full verification

**Files:** `README.md`, `src/HelpOverlay.qml` if platforms are listed, `src/mcpserver.cpp` descriptions.

- [ ] Update text; `bin/build`; `bin/test` all pass; launch offscreen sanity via tests. Commit.
