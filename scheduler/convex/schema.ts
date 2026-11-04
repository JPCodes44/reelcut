import { defineSchema, defineTable } from "convex/server";
import { v } from "convex/values";

export const status = v.union(
  v.literal("scheduled"),
  v.literal("publishing"),
  v.literal("published"),
  v.literal("failed"),
  v.literal("cancelled"),
);

export default defineSchema({
  // One reel waiting to go out, or the record of how it went.
  posts: defineTable({
    title: v.string(),
    caption: v.string(),
    publishAt: v.number(), // ms since epoch
    status,
    // The video, kept here until Instagram has fetched it.
    storageId: v.optional(v.id("_storage")),
    // Who it posts as. The token is never sent back to a client.
    igUserId: v.string(),
    username: v.string(),
    token: v.string(),
    apiBase: v.string(),
    // Progress once it's publishing.
    container: v.optional(v.string()),
    polls: v.optional(v.number()),
    job: v.optional(v.id("_scheduled_functions")),
    error: v.optional(v.string()),
    permalink: v.optional(v.string()),
    publishedAt: v.optional(v.number()),
  })
    .index("by_status", ["status", "publishAt"])
    .index("by_publishAt", ["publishAt"]),
});
