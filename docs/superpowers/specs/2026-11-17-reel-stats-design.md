# Reel stats

A Stats page that shows every reel on every connected account (Instagram,
Facebook Page, YouTube Shorts), with as many metrics as each platform's API
gives, as readable charts and a table. It covers all reels on the account, not
only ones Reelcut posted.

## Data layer: `Stats` (src/stats.h, src/stats.cpp)

A `QObject` built in main.cpp next to the Backend, given the three publishers
(`backend.instagram()`, `backend.facebook()`, `backend.youtube()`) and exposed to
QML as the context property `stats`. It never blocks the GUI thread and never
throws; every network call goes through `Net::whenDone(reply, this, ...)`.

### QML interface (the contract the page is written against)

```
Q_PROPERTY(bool busy READ busy NOTIFY changed)
Q_PROPERTY(QString status READ status NOTIFY changed)        // "Loading Instagram reels (12/40)…", or ""
Q_PROPERTY(QDateTime refreshed READ refreshed NOTIFY changed) // invalid until a refresh finished
Q_PROPERTY(QVariantList reels READ reels NOTIFY changed)
Q_PROPERTY(QVariantList accounts READ accounts NOTIFY changed)
Q_PROPERTY(QVariantList metricInfo READ metricInfo CONSTANT)
Q_INVOKABLE void refresh();
Q_INVOKABLE QVariantList history(const QString &platform, const QString &id) const;
Q_INVOKABLE void loadDaily(const QString &platform, const QString &id); // emits dailyLoaded
signal dailyLoaded(QString platform, QString id, QVariantList points)
```

`reels`: one map per reel, newest first:

```
{ platform: "instagram"|"facebook"|"youtube", id, caption, title,
  thumbnail (url string, may be ""), permalink (url string),
  published (ms since epoch), duration (seconds, 0 if unknown),
  metrics: { <key>: number, ... },   // only the keys the platform gave
  engagementRate: number|undefined } // (likes+comments+shares+saves) / views, 0..1
```

Metric keys (`metricInfo` lists each as `{key, label, unit, platforms}` with
unit one of "count", "seconds", "percent"):

| key | label | Instagram | Facebook | YouTube |
|---|---|---|---|---|
| views | Views | views | fb_reels_total_plays | statistics.viewCount |
| reach | Accounts reached | reach | post_impressions_unique | |
| likes | Likes | likes | post_video_social_actions like / reactions | statistics.likeCount |
| comments | Comments | comments | social actions comment | statistics.commentCount |
| shares | Shares | shares | social actions share | analytics shares |
| saves | Saves | saved | | |
| interactions | Total interactions | total_interactions | | |
| replays | Replays | | fb_reels_replay_count | |
| initialPlays | Initial plays | | blue_reels_play_count | |
| avgWatch | Avg watch time | ig_reels_avg_watch_time (ms→s) | post_video_avg_time_watched (ms→s) | averageViewDuration |
| totalWatch | Total watch time | ig_reels_video_view_total_time (ms→s) | post_video_view_time (ms→s) | estimatedMinutesWatched×60 |
| avgViewPct | Avg % viewed | | | averageViewPercentage |
| followsGained | Followers gained | | post_video_followers | subscribersGained |
| subsLost | Subscribers lost | | | subscribersLost |

`accounts`: one map per platform, always all three:

```
{ platform, label ("Instagram"), connected (bool), account (name),
  followers (number or undefined), reelCount,
  error (string, "" if fine),
  needsPermission (string, "" or the permission/scope to add, e.g.
                   "instagram_business_manage_insights" or "yt-analytics.readonly"),
  followerSeries: [{t: ms, v: number}],   // daily, oldest first, may be empty
  viewSeries:     [{t: ms, v: number}] }  // daily account views/reach, may be empty
```

`history(platform, id)`: the snapshots Reelcut kept of that reel, oldest first,
`[{t: ms, metrics: {...}}]`. `loadDaily` asks YouTube Analytics for that video's
per-day views (`[{t, v}]`); for other platforms it answers with the history's
views instead, so the page can always call it.

### Fetching

All three are fetched in parallel by `refresh()`. Lists are paged up to 500
reels per platform. Per-reel insight calls are limited to 4 in flight per platform.

- **Instagram** (token, userId, apiBase from `InstagramApi`):
  `GET /<user>?fields=followers_count,media_count`;
  `GET /<user>/media?fields=id,caption,media_type,media_product_type,permalink,thumbnail_url,media_url,timestamp,like_count,comments_count&limit=50`
  following `paging.next`, keeping `media_product_type == "REELS"`;
  per reel `GET /<id>/insights?metric=views,reach,likes,comments,shares,saved,total_interactions,ig_reels_avg_watch_time,ig_reels_video_view_total_time`.
  Account `GET /<user>/insights?metric=follower_count,reach&period=day&since=<30d ago>&until=<now>` for the series.
- **Facebook** (page id, page token from `FacebookApi`):
  `GET /<page>?fields=followers_count,fan_count`;
  `GET /<page>/video_reels?fields=id,description,title,created_time,permalink_url,length,picture&limit=50` paged;
  per reel `GET /<id>/video_insights?metric=blue_reels_play_count,fb_reels_total_plays,fb_reels_replay_count,post_impressions_unique,post_video_avg_time_watched,post_video_view_time,post_video_followers,post_video_social_actions`.
  Page series `GET /<page>/insights?metric=page_follows,page_video_views&period=day&since&until` (whichever come back).
- **YouTube** (access token from `YouTubeApi`, renewed as publishing does):
  `channels?part=snippet,contentDetails,statistics&mine=true` → uploads playlist and subscriberCount;
  `playlistItems?part=contentDetails&playlistId=<uploads>&maxResults=50` paged;
  `videos?part=snippet,statistics,contentDetails&id=<≤50 ids>`, keeping videos of 180 s or less (Shorts);
  YouTube Analytics `https://youtubeanalytics.googleapis.com/v2/reports?ids=channel==MINE&startDate=2005-01-01&endDate=<today>&metrics=views,estimatedMinutesWatched,averageViewDuration,averageViewPercentage,likes,comments,shares,subscribersGained,subscribersLost&dimensions=video&filters=video==<id,id,…>` (≤200 ids per call) for the per-video extras, and `dimensions=day` over the last 90 days for the channel series (views and subscribersGained−subscribersLost cumulated onto the current subscriber count for followerSeries).

**Metrics the account can't give.** Meta rejects a whole insights call when one
metric isn't available for that media (older reels, missing permission). Then
retry that reel one metric at a time and remember, for the rest of the refresh,
which metrics fail, so later reels don't repeat it. A permission error
(code 10 / 200 / "permission") sets `needsPermission` and skips insights for
the rest of that platform, keeping the list and basic counts (like_count,
comments_count). Google 403 `insufficientPermissions`/`ACCESS_TOKEN_SCOPE_INSUFFICIENT`
from Analytics sets `needsPermission` to `yt-analytics.readonly` and keeps the
Data API statistics. A platform that's not connected is `connected: false`
with no error. One platform failing never stops the others.

YouTube sign-in asks for `yt-analytics.readonly` too from now on (added to the
scope in youtubeapi.cpp); existing connections keep working without it and show
the reconnect hint.

### Storage

`Net::stateFile(stateDir, "stats.json")`: the last refresh's reels and accounts
(so the page opens with data at once) and the history: per reel key
`platform:id`, a snapshot `{t, metrics}` appended on each refresh when any
metric changed, at most 400 per reel (oldest dropped). Written with
`Net::savePrivate`. A corrupt file is ignored.

### Testability

Base URL overrides like the publishers: `graphBase` (Instagram and Facebook use
their publisher's), `youtubeAnalyticsBase`, and `stateDir`. Tests use stand-in
servers like FakeInstagram.

## Page: StatsPage.qml

Opened from a "Stats" ToolButton in the top bar (next to Downloads), as a page
covering the editor, closed with a Close button or Esc. Built from `stats`
only. Calls `stats.refresh()` when opened if never refreshed or older than 15 minutes.

1. **Header:** title, platform filter (All / Instagram / Facebook / YouTube with
   SocialIcon), period (7 / 28 / 90 days / All, filtering reels by publish
   date), Refresh button with `status` and "Updated 5 min ago".
2. **Account notices:** per account with `error` or `needsPermission`, one line
   saying what to do (e.g. "Add the instagram_business_manage_insights
   permission to your token and reconnect to see views, reach and watch time.").
   Not connected: "Connect Facebook in the Post dialog to see its reels."
3. **Summary cards:** total views, likes, comments, shares, saves, avg watch time,
   avg engagement rate over the filtered reels, plus followers per account; each
   card has a big number and a sparkline when a series exists.
4. **Views over time:** a line chart of each account's `viewSeries` (one line per
   platform, platform colors, legend), with labelled axes, gridlines and a
   hover crosshair showing exact values.
5. **Top reels:** horizontal bar chart of the top 10 filtered reels by a chosen
   metric (dropdown from metricInfo), caption labels, value at the end of each bar.
6. **All reels table:** thumbnail, caption (elided), platform icon, date, then
   one column per metric present in the filtered reels; click a header to sort;
   numbers abbreviated (12.3K) with exact value in a tooltip; seconds as "0:14";
   percent as "38%".
7. **Reel detail** (click a row): side panel with thumbnail, full caption, date,
   open-on-platform link, every metric as a labelled grid, and a growth line chart
   from `loadDaily` (YouTube) or `history` (others) with "Reelcut records growth
   each time stats refresh" when fewer than 2 points.

Charts are QML components drawn with Canvas (no Qt Graphs/Charts on the system):
`LineChart.qml` (series of `{t, v}`, multiple lines, axes, crosshair tooltip),
`BarChart.qml` (labelled horizontal bars), `Sparkline.qml`. Text stays readable:
theme colors (the `theme` context property and the colors Main.qml uses), at
least 12 px, numbers right-aligned, nothing that only color conveys.

## Out of scope

TikTok (no connection exists). Comparing edits to performance. Exporting stats.
