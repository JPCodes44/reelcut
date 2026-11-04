declare const process: { env: Record<string, string | undefined> };

import { ConvexError } from "convex/values";

// One shared secret, set with `npx convex env set REELCUT_SECRET ...` and
// pasted into Reelcut. Every public function checks it: without it, anyone
// who found the deployment address could post as you.
export function checkSecret(secret: string) {
  const expected = process.env.REELCUT_SECRET;
  if (!expected || expected.length < 16)
    throw new ConvexError("The scheduler has no REELCUT_SECRET set (16+ characters).");
  if (!timingSafeEqual(secret, expected)) throw new ConvexError("Wrong scheduler key.");
}

function timingSafeEqual(a: string, b: string) {
  let diff = a.length ^ b.length;
  for (let i = 0; i < Math.max(a.length, b.length); i++)
    diff |= (a.charCodeAt(i) || 0) ^ (b.charCodeAt(i) || 0);
  return diff === 0;
}
