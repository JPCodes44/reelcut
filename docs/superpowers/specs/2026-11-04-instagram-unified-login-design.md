# Instagram token compatibility

Connect Reelcut to the user's existing Meta-generated token. The current integration sends all tokens to graph.instagram.com and reads /me as an Instagram user. The user's new Meta app uses Unified Login, whose tokens require graph.facebook.com/v26.0 and /me/assigned_instagram_accounts.

Keep the existing Qt/C++ integration and Instagram Login behavior. Route EAA tokens through Unified Login, discover exactly one assigned Instagram account, persist that route with the validated account, and use it for publishing after restart. Do not auto-renew Unified Login tokens through the Instagram refresh endpoint. Empty, ambiguous, or malformed account lists must fail without replacing an existing connection. Preserve exact account ID strings.

Alternatives: generating an older Instagram token is unavailable for the user's new app; changing only the API host leaves account discovery and renewal broken. Support both flows in the existing adapter instead. No credential is embedded in source or test fixtures. No automatic publishing or closing the user's running editor.

Reference: https://developers.facebook.com/documentation/instagram-platform/instagram-api-with-unified-login/authentication

Authorization: user asked to complete Instagram connection and confirmed all necessary steps; implement the compatibility fix in place, preserving existing uncommitted work.
