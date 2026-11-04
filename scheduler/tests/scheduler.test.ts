import { convexTest } from "convex-test";
import { afterEach, beforeEach, describe, expect, test, vi } from "vitest";
import { api, internal } from "../convex/_generated/api";
import schema from "../convex/schema";
import { MAX_POLLS } from "../convex/publish";

const modules = import.meta.glob("../convex/**/*.ts");
const SECRET = "test-secret-0123456789";
const BASE = "https://graph.instagram.com";

type Call = { method: string; url: string; params: URLSearchParams };

// A stand-in for Instagram's API: answers by path, remembers the calls.
function fakeInstagram(statuses: string[] = ["IN_PROGRESS", "FINISHED"], opts: { publishError?: string } = {}) {
  const calls: Call[] = [];
  vi.stubGlobal("fetch", async (input: string, init?: RequestInit) => {
    const url = new URL(input);
    const method = init?.method ?? "GET";
    const params = method === "GET" ? url.searchParams : new URLSearchParams(String(init?.body));
    calls.push({ method, url: url.pathname, params });
    const json = (o: unknown) => new Response(JSON.stringify(o), { status: 200 });
    if (url.pathname === "/17841400000000001/media") return json({ id: "C1" });
    if (url.pathname === "/C1") return json({ status_code: statuses.shift() ?? "FINISHED" });
    if (url.pathname === "/17841400000000001/media_publish")
      return opts.publishError
        ? new Response(JSON.stringify({ error: { message: opts.publishError } }), { status: 400 })
        : json({ id: "M1" });
    if (url.pathname === "/M1") return json({ permalink: "https://www.instagram.com/reel/xyz/" });
    return new Response("{}", { status: 404 });
  });
  return calls;
}

async function setup() {
  const t = convexTest(schema, modules);
  const storageId = await t.run((ctx) => ctx.storage.store(new Blob(["video"], { type: "video/mp4" })));
  return { t, storageId };
}

function postArgs(storageId: any, publishAt: number) {
  return {
    secret: SECRET,
    storageId,
    title: "my reel",
    caption: "hello #reel",
    publishAt,
    igUserId: "17841400000000001",
    username: "textink.ai",
    token: "IGQ-token",
    apiBase: BASE,
  };
}

beforeEach(() => {
  vi.useFakeTimers();
  vi.stubEnv("REELCUT_SECRET", SECRET);
});
afterEach(() => {
  vi.useRealTimers();
  vi.unstubAllGlobals();
  vi.unstubAllEnvs();
});

describe("scheduling", () => {
  test("publishes at its time, then forgets the video and token", async () => {
    const calls = fakeInstagram();
    const { t, storageId } = await setup();
    const at = Date.now() + 3600_000;
    const post = await t.mutation(api.posts.schedule, postArgs(storageId, at));
    expect(post.status).toBe("scheduled");
    expect(post).not.toHaveProperty("token");

    vi.advanceTimersByTime(3599_000);
    await t.finishInProgressScheduledFunctions();
    expect(calls).toHaveLength(0); // not before its time

    await t.finishAllScheduledFunctions(vi.runAllTimers);
    const [done] = await t.query(api.posts.list, { secret: SECRET });
    expect(done.status).toBe("published");
    expect(done.permalink).toBe("https://www.instagram.com/reel/xyz/");

    const create = calls[0];
    expect(create.url).toBe("/17841400000000001/media");
    expect(create.params.get("media_type")).toBe("REELS");
    expect(create.params.get("caption")).toBe("hello #reel");
    expect(create.params.get("video_url")).toMatch(/^https?:\/\//);
    expect(create.params.get("access_token")).toBe("IGQ-token");
    expect(calls.map((c) => c.url)).toEqual(["/17841400000000001/media", "/C1", "/C1", "/17841400000000001/media_publish", "/M1"]);

    const row = await t.run((ctx) => ctx.db.get(post.id));
    expect(row!.token).toBe("");
    expect(row!.storageId).toBeUndefined();
    expect(await t.run((ctx) => ctx.storage.getUrl(storageId))).toBeNull();
  });

  test("cancelled posts never go out", async () => {
    const calls = fakeInstagram();
    const { t, storageId } = await setup();
    const post = await t.mutation(api.posts.schedule, postArgs(storageId, Date.now() + 60_000));
    await t.mutation(api.posts.cancel, { secret: SECRET, id: post.id });
    await t.finishAllScheduledFunctions(vi.runAllTimers);
    expect(calls).toHaveLength(0);
    const [row] = await t.query(api.posts.list, { secret: SECRET });
    expect(row.status).toBe("cancelled");
  });

  test("rescheduling moves it", async () => {
    const calls = fakeInstagram();
    const { t, storageId } = await setup();
    const post = await t.mutation(api.posts.schedule, postArgs(storageId, Date.now() + 60_000));
    await t.mutation(api.posts.reschedule, { secret: SECRET, id: post.id, publishAt: Date.now() + 7200_000 });
    vi.advanceTimersByTime(120_000);
    await t.finishInProgressScheduledFunctions();
    expect(calls).toHaveLength(0);
    await t.finishAllScheduledFunctions(vi.runAllTimers);
    const [row] = await t.query(api.posts.list, { secret: SECRET });
    expect(row.status).toBe("published");
  });

  test("a failure keeps the video so it can be retried", async () => {
    fakeInstagram(["FINISHED"], { publishError: "Token expired" });
    const { t, storageId } = await setup();
    const post = await t.mutation(api.posts.schedule, postArgs(storageId, Date.now() + 1000));
    await t.finishAllScheduledFunctions(vi.runAllTimers);
    let [row] = await t.query(api.posts.list, { secret: SECRET });
    expect(row.status).toBe("failed");
    expect(row.error).toContain("Token expired");

    fakeInstagram(["FINISHED"]);
    await t.mutation(api.posts.retry, { secret: SECRET, id: post.id, token: "new-token" });
    await t.finishAllScheduledFunctions(vi.runAllTimers);
    [row] = await t.query(api.posts.list, { secret: SECRET });
    expect(row.status).toBe("published");
  });

  test("a video Instagram rejects fails with its reason", async () => {
    fakeInstagram(["ERROR"]);
    const { t, storageId } = await setup();
    await t.mutation(api.posts.schedule, postArgs(storageId, Date.now()));
    await t.finishAllScheduledFunctions(vi.runAllTimers);
    const [row] = await t.query(api.posts.list, { secret: SECRET });
    expect(row.status).toBe("failed");
    expect(row.error).toContain("couldn't use the video");
  });

  test("gives up if processing never finishes", async () => {
    fakeInstagram(Array(MAX_POLLS + 5).fill("IN_PROGRESS"));
    const { t, storageId } = await setup();
    await t.mutation(api.posts.schedule, postArgs(storageId, Date.now()));
    await t.finishAllScheduledFunctions(vi.runAllTimers, 200);
    const [row] = await t.query(api.posts.list, { secret: SECRET });
    expect(row.status).toBe("failed");
    expect(row.error).toContain("too long");
  });

  test("a post can't be started twice", async () => {
    const { t, storageId } = await setup();
    const post = await t.mutation(api.posts.schedule, postArgs(storageId, Date.now() + 3600_000));
    expect(await t.mutation(internal.publish.claim, { id: post.id })).not.toBeNull();
    expect(await t.mutation(internal.publish.claim, { id: post.id })).toBeNull();
  });
});

describe("guarding", () => {
  test("every call needs the key", async () => {
    const { t, storageId } = await setup();
    await expect(t.query(api.posts.list, { secret: "nope" })).rejects.toThrow(/Wrong scheduler key/);
    await expect(t.mutation(api.posts.uploadUrl, { secret: "" })).rejects.toThrow(/Wrong scheduler key/);
    await expect(t.mutation(api.posts.schedule, { ...postArgs(storageId, Date.now() + 1000), secret: "x" })).rejects.toThrow();
  });

  test("a deployment without a key refuses everything", async () => {
    vi.stubEnv("REELCUT_SECRET", "");
    const { t } = await setup();
    await expect(t.query(api.posts.list, { secret: "" })).rejects.toThrow(/no REELCUT_SECRET/);
  });

  test("rejects bad times, accounts and API hosts", async () => {
    const { t, storageId } = await setup();
    const now = Date.now();
    await expect(t.mutation(api.posts.schedule, postArgs(storageId, now - 3600_000))).rejects.toThrow(/already passed/);
    await expect(t.mutation(api.posts.schedule, postArgs(storageId, now + 80 * 86400_000))).rejects.toThrow(/75 days/);
    await expect(t.mutation(api.posts.schedule, { ...postArgs(storageId, now + 1000), igUserId: "../me" })).rejects.toThrow(/account id/);
    await expect(t.mutation(api.posts.schedule, { ...postArgs(storageId, now + 1000), apiBase: "https://evil.example" })).rejects.toThrow(/API address/);
  });

  test("the token never comes back", async () => {
    fakeInstagram();
    const { t, storageId } = await setup();
    await t.mutation(api.posts.schedule, postArgs(storageId, Date.now() + 1000));
    const list = await t.query(api.posts.list, { secret: SECRET });
    expect(JSON.stringify(list)).not.toContain("IGQ-token");
  });
});
