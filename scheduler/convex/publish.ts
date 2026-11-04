// Publishing a scheduled reel, run by Convex's scheduler at its time.
//
// Instagram fetches the video itself from the file's address here
// (video_url), so nothing has to be online on your side:
//   1. POST /<ig id>/media          media_type=REELS, video_url, caption -> container
//   2. GET  /<container>            status_code, every 20s until FINISHED (or ERROR)
//   3. POST /<ig id>/media_publish  creation_id=<container> -> the post's id
//   4. GET  /<post>                 permalink
// Each step is its own short action, so a slow upload never hits a timeout.
import { v } from "convex/values";
import { internalAction, internalMutation, internalQuery } from "./_generated/server";
import { internal } from "./_generated/api";
import type { Doc } from "./_generated/dataModel";
import type { ActionCtx } from "./_generated/server";

export const POLL_MS = 20_000;
export const MAX_POLLS = 45; // fifteen minutes

type Graph = Record<string, any>;

async function graph(base: string, path: string, token: string, params: Record<string, string>, method: "GET" | "POST") {
  const body = new URLSearchParams({ ...params, access_token: token });
  const res =
    method === "GET"
      ? await fetch(`${base}${path}?${body}`)
      : await fetch(`${base}${path}`, {
          method: "POST",
          headers: { "Content-Type": "application/x-www-form-urlencoded" },
          body,
        });
  const text = await res.text();
  let json: Graph = {};
  try {
    json = JSON.parse(text);
  } catch {
    // not JSON: reported below
  }
  const error = json?.error?.message ?? (res.ok ? null : `HTTP ${res.status}`);
  return { json, text, error: error as string | null };
}

// ---- Database steps ----

export const get = internalQuery({
  args: { id: v.id("posts") },
  handler: async (ctx, { id }) => await ctx.db.get(id),
});

// scheduled -> publishing, once: a post can never be started twice.
export const claim = internalMutation({
  args: { id: v.id("posts") },
  handler: async (ctx, { id }) => {
    const post = await ctx.db.get(id);
    if (!post || post.status !== "scheduled" || !post.storageId) return null;
    const videoUrl = await ctx.storage.getUrl(post.storageId);
    if (!videoUrl) {
      await ctx.db.patch(id, { status: "failed", error: "The video is gone from storage.", job: undefined });
      return null;
    }
    await ctx.db.patch(id, { status: "publishing", job: undefined, polls: 0 });
    return { ...post, videoUrl };
  },
});

export const update = internalMutation({
  args: {
    id: v.id("posts"),
    container: v.optional(v.string()),
    polls: v.optional(v.number()),
  },
  handler: async (ctx, { id, ...fields }) => {
    await ctx.db.patch(id, fields);
  },
});

export const fail = internalMutation({
  args: { id: v.id("posts"), error: v.string() },
  handler: async (ctx, { id, error }) => {
    // The video stays, so the post can be tried again.
    await ctx.db.patch(id, { status: "failed", error });
  },
});

export const done = internalMutation({
  args: { id: v.id("posts"), permalink: v.string() },
  handler: async (ctx, { id, permalink }) => {
    const post = await ctx.db.get(id);
    if (!post) return;
    if (post.storageId) await ctx.storage.delete(post.storageId);
    await ctx.db.patch(id, {
      status: "published",
      permalink,
      publishedAt: Date.now(),
      storageId: undefined,
      token: "", // no longer needed
      error: undefined,
    });
  },
});

// ---- Instagram steps ----

export const start = internalAction({
  args: { id: v.id("posts") },
  handler: async (ctx, { id }): Promise<void> => {
    const post = await ctx.runMutation(internal.publish.claim, { id });
    if (!post) return;
    try {
      const { json, error } = await graph(
        post.apiBase,
        `/${post.igUserId}/media`,
        post.token,
        { media_type: "REELS", video_url: post.videoUrl, caption: post.caption },
        "POST",
      );
      const container = json.id as string | undefined;
      if (error || !container)
        return void await ctx.runMutation(internal.publish.fail, {
          id,
          error: `Instagram wouldn't start the reel: ${error ?? "no container came back"}`,
        });
      await ctx.runMutation(internal.publish.update, { id, container, polls: 0 });
      await ctx.scheduler.runAfter(POLL_MS, internal.publish.poll, { id });
    } catch (e) {
      await ctx.runMutation(internal.publish.fail, { id, error: `Couldn't reach Instagram: ${String(e)}` });
    }
  },
});

export const poll = internalAction({
  args: { id: v.id("posts") },
  handler: async (ctx, { id }): Promise<void> => {
    const post: Doc<"posts"> | null = await ctx.runQuery(internal.publish.get, { id });
    if (!post || post.status !== "publishing" || !post.container) return;
    const polls = (post.polls ?? 0) + 1;
    try {
      const { json, error } = await graph(post.apiBase, `/${post.container}`, post.token, { fields: "status_code,status" }, "GET");
      const status = json.status_code as string | undefined;
      if (error) {
        // A hiccup checking isn't a failed upload: keep checking, within limits.
        if (polls >= MAX_POLLS)
          return void await ctx.runMutation(internal.publish.fail, { id, error: `Couldn't check on the upload: ${error}` });
      } else if (status === "FINISHED") {
        return await publishContainer(ctx, post);
      } else if (status === "ERROR" || status === "EXPIRED") {
        return void await ctx.runMutation(internal.publish.fail, {
          id,
          error: `Instagram couldn't use the video: ${json.status ?? status}`,
        });
      } else if (polls >= MAX_POLLS) {
        return void await ctx.runMutation(internal.publish.fail, { id, error: "Instagram took too long to process the video." });
      }
      await ctx.runMutation(internal.publish.update, { id, polls });
      await ctx.scheduler.runAfter(POLL_MS, internal.publish.poll, { id });
    } catch (e) {
      await ctx.runMutation(internal.publish.fail, { id, error: `Couldn't reach Instagram: ${String(e)}` });
    }
  },
});

async function publishContainer(ctx: ActionCtx, post: Doc<"posts">): Promise<void> {
  const { json, error } = await graph(
    post.apiBase,
    `/${post.igUserId}/media_publish`,
    post.token,
    { creation_id: post.container ?? "" },
    "POST",
  );
  const media = json.id as string | undefined;
  if (error || !media)
    return void await ctx.runMutation(internal.publish.fail, {
      id: post._id,
      error: `Instagram didn't publish it: ${error ?? "no post came back"}`,
    });
  // Posted. The link is a nicety: a failure here doesn't undo anything.
  let permalink = "";
  try {
    const link = await graph(post.apiBase, `/${media}`, post.token, { fields: "permalink" }, "GET");
    permalink = (link.json.permalink as string) ?? "";
  } catch {
    // keep going without it
  }
  await ctx.runMutation(internal.publish.done, { id: post._id, permalink });
}
