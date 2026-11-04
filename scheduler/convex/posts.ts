import { ConvexError, v } from "convex/values";
import { mutation, query } from "./_generated/server";
import { internal } from "./_generated/api";
import { checkSecret } from "./auth";
import type { Doc } from "./_generated/dataModel";

const MAX_AHEAD_MS = 75 * 24 * 3600 * 1000; // as far ahead as Instagram's own planner

// What a client sees of a post: everything but the token.
function view(p: Doc<"posts">) {
  return {
    id: p._id,
    title: p.title,
    caption: p.caption,
    publishAt: p.publishAt,
    status: p.status,
    username: p.username,
    error: p.error ?? null,
    permalink: p.permalink ?? null,
    publishedAt: p.publishedAt ?? null,
  };
}

// Where to upload the video before scheduling it.
export const uploadUrl = mutation({
  args: { secret: v.string() },
  handler: async (ctx, { secret }) => {
    checkSecret(secret);
    return await ctx.storage.generateUploadUrl();
  },
});

export const schedule = mutation({
  args: {
    secret: v.string(),
    storageId: v.id("_storage"),
    title: v.string(),
    caption: v.string(),
    publishAt: v.number(),
    igUserId: v.string(),
    username: v.string(),
    token: v.string(),
    apiBase: v.string(),
  },
  handler: async (ctx, { secret, ...post }) => {
    checkSecret(secret);
    const now = Date.now();
    if (!/^\d+$/.test(post.igUserId)) throw new ConvexError("That isn't an Instagram account id.");
    if (!/^https:\/\/graph\.(instagram|facebook)\.com(\/v\d+\.\d+)?$/.test(post.apiBase))
      throw new ConvexError("Unknown Instagram API address.");
    if (post.publishAt < now - 60_000) throw new ConvexError("That time has already passed.");
    if (post.publishAt > now + MAX_AHEAD_MS) throw new ConvexError("Pick a time within 75 days.");
    if (post.caption.length > 2200) throw new ConvexError("The caption is over 2,200 characters.");
    if (!(await ctx.db.system.get(post.storageId))) throw new ConvexError("The video didn't upload.");
    const id = await ctx.db.insert("posts", { ...post, status: "scheduled" });
    const job = await ctx.scheduler.runAt(Math.max(post.publishAt, now), internal.publish.start, { id });
    await ctx.db.patch(id, { job });
    return view((await ctx.db.get(id))!);
  },
});

// Upcoming posts first, then the latest results.
export const list = query({
  args: { secret: v.string() },
  handler: async (ctx, { secret }) => {
    checkSecret(secret);
    const posts = await ctx.db.query("posts").withIndex("by_publishAt").order("desc").take(100);
    return posts.map(view);
  },
});

export const cancel = mutation({
  args: { secret: v.string(), id: v.id("posts") },
  handler: async (ctx, { secret, id }) => {
    checkSecret(secret);
    const post = await ctx.db.get(id);
    if (!post) throw new ConvexError("No such post.");
    if (post.status !== "scheduled") throw new ConvexError(`It's already ${post.status}.`);
    if (post.job) await ctx.scheduler.cancel(post.job);
    if (post.storageId) await ctx.storage.delete(post.storageId);
    await ctx.db.patch(id, { status: "cancelled", storageId: undefined, job: undefined, token: "" });
    return view((await ctx.db.get(id))!);
  },
});

// Move a waiting post to another time.
export const reschedule = mutation({
  args: { secret: v.string(), id: v.id("posts"), publishAt: v.number() },
  handler: async (ctx, { secret, id, publishAt }) => {
    checkSecret(secret);
    const post = await ctx.db.get(id);
    if (!post) throw new ConvexError("No such post.");
    if (post.status !== "scheduled") throw new ConvexError(`It's already ${post.status}.`);
    const now = Date.now();
    if (publishAt < now - 60_000) throw new ConvexError("That time has already passed.");
    if (publishAt > now + MAX_AHEAD_MS) throw new ConvexError("Pick a time within 75 days.");
    if (post.job) await ctx.scheduler.cancel(post.job);
    const job = await ctx.scheduler.runAt(Math.max(publishAt, now), internal.publish.start, { id });
    await ctx.db.patch(id, { publishAt, job });
    return view((await ctx.db.get(id))!);
  },
});

// Forget finished, failed and cancelled posts.
export const remove = mutation({
  args: { secret: v.string(), id: v.id("posts") },
  handler: async (ctx, { secret, id }) => {
    checkSecret(secret);
    const post = await ctx.db.get(id);
    if (!post) return null;
    if (post.status === "scheduled" || post.status === "publishing")
      throw new ConvexError("Cancel it first.");
    if (post.storageId) await ctx.storage.delete(post.storageId);
    await ctx.db.delete(id);
    return null;
  },
});

// Try a failed post again, now or at a new time. Needs the video still kept
// and a fresh token if the old one was the problem.
export const retry = mutation({
  args: { secret: v.string(), id: v.id("posts"), publishAt: v.optional(v.number()), token: v.optional(v.string()) },
  handler: async (ctx, { secret, id, publishAt, token }) => {
    checkSecret(secret);
    const post = await ctx.db.get(id);
    if (!post) throw new ConvexError("No such post.");
    if (post.status !== "failed") throw new ConvexError(`It's ${post.status}, not failed.`);
    if (!post.storageId) throw new ConvexError("The video is gone; schedule it again from Reelcut.");
    const at = Math.max(publishAt ?? Date.now(), Date.now());
    const job = await ctx.scheduler.runAt(at, internal.publish.start, { id });
    await ctx.db.patch(id, {
      status: "scheduled",
      publishAt: at,
      job,
      error: undefined,
      container: undefined,
      polls: undefined,
      ...(token ? { token } : {}),
    });
    return view((await ctx.db.get(id))!);
  },
});
