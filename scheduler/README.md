# Reelcut scheduler

Instagram's API can only publish right away, so scheduled posts go through this small
[Convex](https://convex.dev) backend. Reelcut uploads the exported reel to Convex file
storage and records the post. At the time you picked, Convex's scheduler has Instagram
fetch the video (`video_url`), checks every 20 s until it's processed, publishes it, then
deletes the video and the token it was given. Your computer can be off by then.

## Setup (once)

```sh
cd scheduler
npm install
npx convex deploy                                     # makes the production deployment
npx convex env set --prod REELCUT_SECRET "$(openssl rand -hex 24)"
```

Then in Reelcut go to Post → Schedule for later → Set up…, and paste the deployment URL
(`https://<name>.convex.cloud`) and the same secret. They're saved in
`~/.config/reelcut/scheduler.json` (mode 600).

## Notes

- Every function checks the secret. Without it, anyone who knows the URL can do nothing.
- A post stores a copy of your Instagram token and uses it when it publishes. If the token
  expires before then, the post fails. Retry sends your current token.
- If a post fails, its video is kept so Retry works. Remove deletes it.
- You can schedule up to 75 days ahead, the same limit as Instagram's own planner.
- `npm test` runs the backend tests against a fake Instagram (convex-test + vitest).
