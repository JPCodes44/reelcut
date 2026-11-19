# Account Settings

User request: a separate Settings page to manage all platform connections outside Post, with expiry dates/times for each token.

Actual application: /home/jp/reelcut-social, selected by the installed desktop launcher. The initial /home/jp/reelcut checkout lacked its Facebook/YouTube/Stats features; prototype changes there were fully restored to the pre-task diff.

Use the existing InstagramApi, FacebookApi and YouTubeApi instances and credential files. Add a non-secret Accounts status model and a standalone SettingsPage reached from the toolbar without clips. All Post connection methods delegate through the same manager. Stats account notices link to the relevant Settings platform. Instagram and Facebook offer token validation/replacement, connection testing, disconnect and Meta tool links. YouTube retains Desktop-app OAuth, shows access-token expiry (auto-renewing) and refresh expiry when Google reports it. TikTok clearly remains browser sign-in.

Expiry: local date/time and timezone plus remaining duration. Meta token expiry and data-access expiry are distinct. Zero explicitly means Never; missing/unsupported means Unknown. Do not ship app secrets or invent expiry dates. Meta lookup failures retain previous reported dates for the same credential; replacement credentials invalidate prior metadata. Google transient channel errors do not mark credentials expired.

Safety/reliability: no tokens in QML models or status messages, owner-only files, preserve working credentials on failed/explicitly invalid replacements, no silent first-Page selection on incomplete paginated discovery, generation guards after disconnect/replacement. Existing publisher storage remains the only credential source. Store status/fingerprints separately. Full Meta OAuth hosting is separate app/backend setup; link official tools instead of a fake sign-in flow.

Settings also exposes the existing Instagram scheduler setup; Facebook/YouTube native scheduling remains unchanged. Preserve unrelated edits in the social checkout. Test offscreen against fake Meta/Google APIs before applying the prepared files, then rebuild the binary used by the launcher.
