# Facebook Reels and YouTube Shorts

Reelcut designs, previews and posts reels for Instagram and TikTok. Add Facebook Reels and YouTube Shorts as
first-class platforms: their app chrome and safe zones in the preview, and publishing (now or scheduled) through
their official APIs, including posting one export to several platforms at once.

## Platforms are a table

Today the platform is a two-way `tiktok ? … : instagram` choice repeated across C++, QML and the MCP server.
`Posting::Target` (src/posting.h) becomes the single list of platforms, in display order
instagram, facebook, youtube, tiktok, each with:

| field | instagram | facebook | youtube | tiktok |
| --- | --- | --- | --- | --- |
| label | Instagram | Facebook | YouTube | TikTok |
| by-hand url | instagram.com | facebook.com/reels/create | studio.youtube.com | tiktok.com/tiktokstudio/upload |
| max caption chars | 2200 | 2200 | 5000 | 2200 |
| max hashtags (0 = none) | 30 | 0 | 60 | 0 |
| duration seconds (0 = none) | 0–0 | 3–90 | 0–180 | 0–0 |
| safe zone left, top, right, bottom (px of 1080×1920) | 40, 220, 950, 1500 | 65, 270, 960, 1250 | 48, 290, 890, 1400 | 40, 160, 930, 1440 |

`Project::setPlatform`, `Project::fromJson`, `captionHome`, the dashed outline in Preview.qml, the toolbar
buttons in Main.qml, the Inspector's "Place for …" label and the MCP `set_platform`/`create_project` enums all
read this table. An unknown platform name falls back to instagram, as today. Backend exposes it to QML as
`platforms` (list) and `platformInfo` (the current one).

## Overlays

`SocialPreview.qml` takes `platform: string` instead of `tiktok: bool` and lays out each app's chrome inside the
same 360×640 logical phone, every part flowing through layouts (`socialTop`, `socialBody`, `socialNav` with five
equal `socialNavCell`s, as the existing layout test requires):

- **Facebook Reels**: "Reels" title with search and profile icons; Page avatar, Page name and "Follow"; caption
  (2 lines) and hashtags; "Original audio"; right rail of like (thumb), comment, share, more; nav of home,
  video, friends, notifications, menu.
- **YouTube Shorts**: search and more icons at the top; right rail of like, dislike, comment, share, remix and a
  square sound thumbnail; channel avatar, @channel and a white "Subscribe" pill; the title (YouTube shows the
  title, not the description) on 2 lines; nav of Home, Shorts, create (+), Subscriptions, You.

New icons go into `SocialIcon.qml` as SVG paths. The username shown is the connected account for that platform
(Instagram username, Facebook Page name, YouTube channel title) or `your_account`. The chrome never appears in
exports, as today.

## Publishing interface

`Publisher` (src/publisher.h) is a small abstract QObject that each API client derives from:

```cpp
struct PostRequest { QString file, text, title; QDateTime when; };  // when invalid: now
class Publisher : public QObject {
    virtual QString platform() const = 0;
    virtual bool connected() const = 0;
    virtual QString account() const = 0;
    virtual bool busy() const = 0;
    virtual void publish(const PostRequest &post) = 0;
signals:
    void accountChanged();
    void progress(const QString &text, double fraction);           // -1: unknown
    void published(bool ok, const QString &message, const QUrl &link);
};
```

`InstagramApi` derives from it (its existing `publish(file, caption)` stays; scheduling remains the Convex
scheduler's job, since Instagram's API has none). `FacebookApi` and `YouTubeApi` are new and schedule natively,
so they never touch Convex.

## Facebook (src/facebookapi.h)

Connecting reuses Meta: `connectToken(userToken)` calls `GET /me/accounts?fields=id,name,access_token` on
graph.facebook.com/v26.0. The dialog offers the connected Instagram token first and a field to paste another
user token. The app needs the `pages_show_list`, `pages_read_engagement` and `pages_manage_posts` permissions.
None: fail with that hint. One: keep it. Several: emit `pagesFound(list of {id, name})` and wait for
`choosePage(id)`. The Page id, name and Page token are kept in `~/.config/reelcut/facebook.json`, mode 600; a
failed connect leaves an existing connection alone. Page ids are kept as strings.

Publishing (Reels Publishing API):

1. `POST /{page}/video_reels` `upload_phase=start` → `video_id`, `upload_url`
2. `POST {upload_url}` with `Authorization: OAuth {page token}`, `offset: 0`, `file_size`, the bytes
   → `{"success": true}`
3. `POST /{page}/video_reels` `upload_phase=finish`, `video_id`, `description`, `video_state=PUBLISHED`, or
   `SCHEDULED` with `scheduled_publish_time` (unix seconds)
4. `GET /{video_id}?fields=status` until `processing_phase.status` is complete and
   `publishing_phase.publish_status` is `published` or `scheduled` (error or `video_status` error/upload_failed:
   fail with its message; 200 tries at `pollIntervalMs`)
5. `GET /{video_id}?fields=permalink_url` → the link (relative paths get `https://www.facebook.com`);
   `https://www.facebook.com/reel/{video_id}` if none comes back.

A scheduled time must be more than 10 minutes and less than 29 days away; Backend refuses others before
exporting.

## YouTube (src/youtubeapi.h)

The user creates a **Desktop app** OAuth client in Google Cloud (YouTube Data API v3 enabled) and pastes its
client id and secret. `connectClient(id, secret)`:

1. listens on `127.0.0.1:<free port>`, opens
   `accounts.google.com/o/oauth2/v2/auth` with `scope=youtube.upload youtube.readonly`, `access_type=offline`,
   `prompt=consent`, a random `state` and a PKCE S256 challenge;
2. the browser comes back with `?code&state`; a wrong state or `error` fails; the page says it can be closed;
3. `POST oauth2.googleapis.com/token` (`grant_type=authorization_code`, the verifier) → access and refresh tokens;
4. `GET youtube/v3/channels?part=snippet&mine=true` → channel id and title;
5. client id, secret, refresh token, channel id and title are kept in `~/.config/reelcut/youtube.json`, mode 600.

Access tokens are refreshed (`grant_type=refresh_token`) when missing or within a minute of expiry; a refresh
refused with `invalid_grant` disconnects and says to connect again (Google expires refresh tokens of OAuth apps
left in "Testing" after 7 days, which the connect page mentions).

Publishing is a resumable upload:

1. `POST upload/youtube/v3/videos?uploadType=resumable&part=snippet,status` with JSON
   `snippet{title, description, categoryId: "22"}`,
   `status{privacyStatus, selfDeclaredMadeForKids: false, publishAt?}`, headers `X-Upload-Content-Length`,
   `X-Upload-Content-Type: video/mp4` → `Location`
2. `PUT {Location}` the bytes as `video/mp4` → the video resource; its `id` gives `https://youtube.com/shorts/{id}`.

Now: `privacyStatus=public`. Scheduled: `privacyStatus=private` and `publishAt` (UTC, RFC 3339); YouTube
publishes it then. The description is the caption and hashtags, with `#Shorts` added when it isn't already
there. The title is required, 1–100 characters, and neither title nor description may contain `<` or `>`.

## Backend: cross-posting

`Backend::postTo(platforms, caption, hashtags, title, publishAtMs)` (Q_INVOKABLE):

- checks every platform with `checkPosts(platforms, caption, hashtags, title)`: the union of each platform's
  problems (characters, hashtags, duration, YouTube title rules); every platform must be connected; a scheduled
  time must suit each one (Instagram: scheduler set up, up to 75 days; Facebook: 10 minutes to 29 days;
  YouTube: in the future);
- exports once (`<title>-<platform>.mp4` for one platform, `<title>-post.mp4` for several);
- then runs the platforms one after another: Instagram scheduled goes to the Convex scheduler, everything else to
  its Publisher. Each finished platform emits `platformPosted(platform, ok, message, link)`; a failure doesn't stop
  the rest. At the end it emits `postScheduled(allOk, summary)` when scheduled, else
  `postPublished(allOk, summary, link)` (the first link). If any failed, the first failed platform's page is
  opened by hand with the caption copied, as Instagram does today.

`post(platform, caption, hashtags, publish, publishAtMs)` stays for the by-hand flow and calls `postTo` when
`publish` is set, so existing callers and tests keep working.

Facebook and YouTube posts scheduled from Reelcut are remembered in `~/.config/reelcut/scheduled-direct.json`
(platform, title, caption, publishAt, link) until their time passes, and listed with the Convex posts in the
dialog's Scheduled list with an **Open** button (to change or cancel them on the platform), since the platforms
hold them, not Reelcut.

## Post dialog

- Platform toggles (`postTo-<id>`) for all four. Instagram, Facebook and YouTube can be combined; TikTok is
  by-hand only and selecting it clears the others (and the reverse).
- **Publishing** means every selected platform is connected (and, for Instagram, "publish directly" is on).
  One unconnected platform alone keeps today's by-hand flow. Several with any unconnected: Next is disabled with
  "Connect X to post to several at once".
- An account row per selected API platform: connected name with Disconnect, or Connect… which opens that
  platform's connect page (`connectFor`).
- A **Title** field (`postTitle`) shows when YouTube is selected, starting from the project title.
- **Schedule for later** applies to all selected; it needs the Convex scheduler only when Instagram is selected.
- The character counter and problems come from `checkPosts`.
- The Published/Scheduled stage lists one line per platform with its result and an **Open** button.
- Connect pages: Facebook (permissions to add, "Use my Instagram connection" or paste a token, then a Page picker
  if several); YouTube (how to make the Desktop OAuth client, two fields, Connect; "Finish signing in in your
  browser…").

## Errors

Every network error carries the platform's own message. Connect failures never replace a working connection.
Tokens live only in the mode-600 files and in request headers or bodies, never in logs, URLs shown to the user,
or source/test fixtures.

## Testing

Stand-in servers on localhost (like `FakeInstagram`) for Facebook (accounts, start, rupload, finish, status,
permalink) and Google (auth redirect driven by the test, token, channels, resumable upload). Tests:

- the platform table drives safe zones, caption home and fromJson for all four;
- the preview lays out Facebook and YouTube chrome inside the phone at three window sizes (existing layout test);
- Facebook: connect with one page, several pages (choose), none; publish now; scheduled sends SCHEDULED and the
  time; an error status fails with its message; the files are mode 600;
- YouTube: the loopback flow with state check and PKCE; refresh; publish now (public) and scheduled (private +
  publishAt), `#Shorts` added once; title rules;
- cross-post Instagram + Facebook + YouTube from the dialog: one export, three results; one failing doesn't stop
  the others; scheduling refuses a Facebook time under 10 minutes.

## Out of scope

Cancelling or rescheduling Facebook/YouTube posts from Reelcut (done on the platform), thumbnails, YouTube
playlists, Facebook personal profiles (the API only posts to Pages), TikTok API posting.
