.pragma library

// Pure local-calendar and display rules, shared by the page and its tests.
var platforms = ["instagram", "facebook", "youtube", "tiktok"];
var targetCount = 10;
var cardMinutes = 48;

// Qt's Date constructor can resolve a missing midnight backwards into the
// previous date. Advance across that gap to the first valid local day hour.
function calendarStart(year, month, date) {
    var civil = new Date(Date.UTC(year, month, date));
    var y = civil.getUTCFullYear(), m = civil.getUTCMonth(), d = civil.getUTCDate();
    var start = new Date(y, m, d);
    for (var i=0; i<24 && (start.getFullYear()!==y || start.getMonth()!==m || start.getDate()!==d); ++i)
        start = new Date(start.getTime()+3600000);
    return start.getTime();
}
function dayStart(value) {
    var d = new Date(value);
    return calendarStart(d.getFullYear(), d.getMonth(), d.getDate());
}
function addDays(value, offset) {
    var d = new Date(value);
    return calendarStart(d.getFullYear(), d.getMonth(), d.getDate()+offset);
}
// count: slots that day (1-10, default 10); YouTube runs fewer because of its upload cap.
function slots(day, startMinute, spacingMinutes, count) {
    var n = count === undefined ? targetCount : count;
    var base = new Date(dayStart(day));
    if (!isFinite(base.getTime()) || !isFinite(startMinute) || !isFinite(spacingMinutes)
            || Math.floor(startMinute) !== startMinute || Math.floor(spacingMinutes) !== spacingMinutes
            || startMinute < 0 || [60,90,120].indexOf(spacingMinutes) < 0
            || Math.floor(n) !== n || n < 1 || n > targetCount
            || startMinute + (n - 1) * spacingMinutes >= 1440) return [];
    var out = [];
    for (var i = 0; i < n; ++i) {
        var minute = startMinute + i * spacingMinutes;
        var date = new Date(base.getFullYear(), base.getMonth(), base.getDate(),
                            Math.floor(minute / 60), minute % 60);
        var valid = date.getDate() === base.getDate() && date.getHours() === Math.floor(minute / 60)
                    && date.getMinutes() === minute % 60;
        out.push({slot: i, minute: minute, ms: valid ? date.getTime() : 0, valid: valid});
    }
    return out;
}
function timeText(minute) {
    return (Math.floor(minute / 60) < 10 ? "0" : "") + Math.floor(minute / 60)
           + ":" + (minute % 60 < 10 ? "0" : "") + minute % 60;
}
function readTime(text) {
    var m = /^\s*(\d{1,2}):(\d{2})\s*$/.exec(String(text));
    if (!m || Number(m[1]) > 23 || Number(m[2]) > 59) return -1;
    return Number(m[1]) * 60 + Number(m[2]);
}
function linkKey(platform, link) {
    var url = String(link || "").trim();
    if (!url) return "";
    var match;
    if (platform === "youtube") {
        match = /(?:youtu\.be\/|youtube\.com\/(?:shorts\/|watch\?(?:[^#]*&)?v=))([^?&#/]+)/i.exec(url);
        if (match) return platform + ":" + match[1];
    } else if (platform === "facebook") {
        match = /facebook\.com\/(?:reel\/|(?:[^/]+\/)?videos\/)(\d+)/i.exec(url)
                || /facebook\.com\/watch\/?\?(?:[^#]*&)?v=(\d+)/i.exec(url);
        if (match) return platform + ":" + match[1];
    } else if (platform === "instagram") {
        match = /instagram\.com\/(?:reel|reels|p)\/([^/?#]+)/i.exec(url);
        if (match) return platform + ":" + match[1];
    }
    return platform + ":" + url.replace(/^https?:\/\/(?:www\.)?/i, "").replace(/#.*$/, "").replace(/\/+$/, "");
}
function dayPosts(day, scheduled, published, now) {
    var from = dayStart(day), until = addDays(from, 1);
    var all = [], identities = {};
    (scheduled || []).forEach(function(post) {
        if (!post || platforms.indexOf(post.platform) < 0) return;
        var ms = Number(post.publishAt);
        if (!isFinite(ms) || ms <= 0) return;
        var p = Object.assign({}, post);
        p.ms = ms;
        p.link = String(post.permalink || post.link || "");
        p.status = post.status || "scheduled";
        if (p.platform !== "instagram" && p.status === "scheduled" && ms <= now) p.status = "awaiting";
        p.title = post.title || post.caption || "";
        p.source = "schedule";
        var identity = linkKey(p.platform, p.link);
        if (identity && identities[identity] !== undefined) return;
        if (identity) identities[identity] = all.length;
        all.push(p);
    });
    (published || []).forEach(function(post) {
        if (!post || platforms.indexOf(post.platform) < 0) return;
        // An owner can see private/scheduled YouTube uploads before publication.
        // Old caches without visibility evidence need a Stats refresh.
        if (post.platform === "youtube" && post.privacyStatus !== "public") return;
        var ms = Number(post.published);
        if (!isFinite(ms) || ms <= 0) return;
        var identity = linkKey(post.platform, post.permalink || post.link);
        // The actual platform id also deduplicates repeated Stats rows.
        var publishedKey = identity || post.platform + ":published:" + post.id;
        var at = identities[publishedKey];
        var p = Object.assign({}, at !== undefined ? all[at] : {}, post);
        p.ms = ms;
        p.status = "published";
        p.link = String(post.permalink || post.link || p.link || "");
        p.title = post.title || post.caption || p.title || "";
        p.source = "published";
        if (at !== undefined) all[at] = p;
        else { identities[publishedKey] = all.length; all.push(p); }
    });
    return all.filter(function(p) { return p.ms >= from && p.ms < until; }).map(function(p) {
        var d = new Date(p.ms);
        p.minute = d.getHours() * 60 + d.getMinutes();
        return p;
    }).sort(function(a,b) { return a.ms - b.ms; });
}
// "y-m-d" keys (local, month 0-based) of every day with a scheduled or published post.
function postDays(scheduled, published) {
    var days = {};
    function mark(ms) {
        ms = Number(ms);
        if (!isFinite(ms) || ms <= 0) return;
        var d = new Date(ms);
        days[d.getFullYear() + "-" + d.getMonth() + "-" + d.getDate()] = true;
    }
    (scheduled || []).forEach(function(p) { if (p && platforms.indexOf(p.platform) >= 0) mark(p.publishAt); });
    (published || []).forEach(function(p) {
        if (!p || platforms.indexOf(p.platform) < 0) return;
        if (p.platform === "youtube" && p.privacyStatus !== "public") return;
        mark(p.published);
    });
    return days;
}
function platformSummary(platform, cards, targetSlots) {
    var summary = {covered:0, scheduled:0, published:0, failed:0, awaiting:0, target:(targetSlots || []).length || targetCount};
    (cards || []).forEach(function(p) {
        if (p.platform !== platform) return;
        if (p.status === "failed") summary.failed++;
        else if (p.status === "published") { summary.published++; summary.covered++; }
        else if (p.status === "awaiting") { summary.awaiting++; summary.covered++; }
        else if (p.status === "scheduled" || p.status === "publishing") { summary.scheduled++; summary.covered++; }
        else if (p.status === "placeholder") { summary.placeholders = (summary.placeholders || 0) + 1; summary.covered++; }
    });
    return summary;
}
function layoutCards(cards) {
    var sorted = (cards || []).map(function(p) { return Object.assign({},p); })
        .sort(function(a,b) { return a.minute - b.minute; });
    var group = [], ends = [], groupEnd = -1;
    function finish() {
        group.forEach(function(p) { p.columns = ends.length; });
        group = []; ends = []; groupEnd = -1;
    }
    sorted.forEach(function(p) {
        if (p.minute >= groupEnd && group.length) finish();
        var column = 0;
        while (column < ends.length && ends[column] > p.minute) column++;
        ends[column] = p.minute + cardMinutes;
        p.column = column;
        groupEnd = Math.max(groupEnd, p.minute + cardMinutes);
        group.push(p);
    });
    finish();
    return sorted;
}

// The UGC remix prompt: one paste that has Claude Code turn reels into Reelcut
// placeholders for a brand, each with its reference reel and a step-by-step
// filming guide. mode "profile" takes every reel on a profile; "reels" takes
// the reel links given, one per line. Blank fields get defaults.
function remixPrompt(source, brand, language, mode) {
    var reels = mode === "reels";
    var links = String(source || "").split(/[\s,]+/).filter(function(l) { return l.length > 0; });
    brand = String(brand || "").trim() || "TextInk (textink.ca)";
    language = String(language || "").trim() || "English";
    var slug = brand.replace(/\(.*\)/, "").trim();
    var target, list;
    if (reels) {
        if (!links.length) links = ["<reel URL>"];
        target = links.length === 1 ? "this reel" : "these " + links.length + " reels";
        list = "1. List: for each reel below, get its id, length, views and title with `yt-dlp --print \"%(id)s %(duration)s %(view_count)s %(title)s\" <url>` (listing only). Done when you have all four for every reel.\n"
             + links.map(function(l) { return "   - " + l; }).join("\n");
    } else {
        links = [links[0] || "<TikTok or Instagram profile URL>"];
        target = "every reel on " + links[0];
        list = "1. List: get every reel URL on " + links[0] + " with `yt-dlp --flat-playlist --print \"%(id)s %(duration)s %(view_count)s %(title)s\"` (listing only). Done when you have each reel's id, length, views and title.";
    }
    var title = reels ? (links.length === 1 ? links[0] : links.length + " reels") : links[0];
    return [
        "UGC remix: " + title + " -> " + brand,
        "",
        "Remake " + target + " for " + brand + ", as close to the original as possible: same cuts, timing, text placement, stickers and music, with " + brand + " shown wherever the old brand was. Hooks in " + language + ". Use the Reelcut MCP tools.",
        "",
        list,
        "2. Download: one Reelcut app_downloads_download call per reel URL. Done when every reel is in ~/Videos/Reelcut Downloads.",
        "3. Study: make an 8-frame contact sheet per reel with ffmpeg (tile=8x1) and read each one. For every reel note the hook text and position, stickers, each shot with its length, the camera look, and every shot where the old brand shows.",
        "4. Plan the hybrid, cheapest first: shots of an app or website on screen get real footage I film (AI video garbles on-screen text); timing, text, stickers and music get rebuilt in Reelcut for free; Higgsfield only for shots with no screen in them, quoted first under my Higgsfield rules. Where a real person appears who isn't me, plan my own footage in their place, never a face swap.",
        "5. Placeholders: copy each reel to ~/Videos/" + slug + " remixes/references/<slug>.mp4 and make an empty ~/Videos/" + slug + " remixes/<slug>/ folder. Get slots with app_backend_next_open_slots for instagram, then call app_backend_add_placeholder once per reel, most-viewed first, with: platform, publishAt, title, reference (the copied file), referenceUrl (the original post), note (what this slot will post), caption (with the " + brand + " link), folder (the clip folder) and guide: the look to match, the hook text and stickers, every shot with its seconds, keeping the old brand out of frame, and the clip folder with clip01.mp4, clip02.mp4 naming.",
        "6. Verify: open the Schedule page on that day, screenshot it, then open one placeholder and confirm the reference reel plays under its REFERENCE flag and the guide reads clearly.",
        "",
        "Placeholders never publish; keep this to placeholders and post nothing. Finish with a table: slot, original reel, views, what I need to film."
    ].join("\n");
}

// The prompt that turns a placeholder's raw clips into the finished reel: built
// from that placeholder's own guide, reference reel, link, folder and caption,
// so pasting it into Claude Code recreates the original's edit on the new clips.
// language: the audience to cater to; empty keeps the original's language.
function editPrompt(post, language) {
    post = post || {};
    language = String(language || "").trim();
    var audience = language ? language + "-speaking viewers" : "the original's audience, in the original's language";
    var title = String(post.title || "Untitled remix");
    var folder = String(post.folder || "").trim() || "<clip folder>";
    var reference = String(post.reference || "").trim();
    var link = String(post.referenceUrl || "").trim();
    var guide = (post.guide || []).map(function(s) { return String(s).trim(); })
        .filter(function(s) { return s.length > 0 && !/^folder:/i.test(s); });
    var when = post.ms ? new Date(post.ms) : null;
    var slot = when ? (String(post.platform || "instagram") + " " + when.getFullYear() + "-" + String(when.getMonth() + 1).padStart(2, "0")
        + "-" + String(when.getDate()).padStart(2, "0") + " " + String(when.getHours()).padStart(2, "0") + ":" + String(when.getMinutes()).padStart(2, "0"))
        : String(post.platform || "instagram");
    var lines = [
        "Edit the remix: " + title,
        "",
        "Make this reel a shot-for-shot copy of the original: same length, cut points, framing, motion, speed, text, stickers, music and look. Only the footage changes: use my raw clips. They are NOT edited: they run long, start and end early or late, may be shot wider or tighter, at another speed or angle. Do every edit needed (trim, reorder within a shot, speed change, crop/zoom/pan to match framing, push-ins, colour match, stabilise feel) so each shot reads like the original's. Use the Reelcut MCP tools.",
        "",
        "Original reel: " + (link || "(no link)"),
        "Reference copy: " + (reference || "(none; download the original with app_downloads_download)"),
        "Raw clips: " + folder + " (clip01.mp4, clip02.mp4 ... in shot order)",
        "Slot: " + slot,
    ];
    if (post.note) lines.push("What this slot posts: " + String(post.note));
    lines.push("Audience: " + audience + ". All on-screen text, the hook and the caption are " + (language ? "in " + language + (language === "Hinglish" ? " (Hindi-English mix in Latin script, as Indian creators write it)" : "") + ", written the way a native creator would, not word-for-word translated" : "in the original's language") + ". This overrides any language the plan below names.");
    if (post.caption) lines.push("Draft caption (fallback only): " + String(post.caption));
    lines.push("", "The filming plan the clips were shot to:");
    if (guide.length)
        guide.forEach(function(s, i) { lines.push("   " + (i + 1) + ". " + s); });
    else
        lines.push("   (none written; work from the reference alone)");
    lines.push("",
        "1. Check the clips: list " + folder + " and probe_media each clip (length, fps, size, rotation). If a shot from the plan has no clip, or a clip is unusable (blurry, old brand in frame, too short for its shot), stop and tell me which one to refilm. Done when every shot maps to a clip.",
        "2. Break down the original: probe the reference, find every cut with ffmpeg scene detection (select='gt(scene,0.25)',showinfo) and write a shot table: start, end, length to 0.05 s, framing (wide/close/top-down), camera move (static, push-in, pan, shake), speed, transition, on-screen text (words, position, size, colour, font look, when it appears and leaves), stickers (what, where, when), and the colour look. Pull a frame at the middle of each shot and read it. Done when the shot lengths add up to the reference length.",
        "3. Pick the takes: for each shot, scrub its raw clip (preview frames every 0.5 s) and choose the stretch that best matches the original's action, the moment of a tap or a reveal landing on the same beat. Cut away fumbles, dead starts and the old brand.",
        "4. Build it: create_project in " + folder + " for " + String(post.platform || "instagram") + ", add the clips in shot order, then per shot set the in/out and speed so it lasts exactly the original's length, and set zoom/pan and motion so framing and camera moves match. Match transitions. Recreate every text overlay and sticker at the same time and place (hook in the language the plan says; the brand in place of the old one). Use the original's audio as the music track, aligned at 0, unless the plan says otherwise. Colour match with set_look against the reference frames. Add the brand end card if the plan asks for one.",
        "5. Compare: for every cut time, preview_frame the edit and pull the reference frame at the same time, stack them side by side in one contact sheet and read it. Fix any shot whose timing, framing, text or sticker is off. Done when total length matches the reference within 0.1 s and every pair lines up.",
        "6. Caption: get the original's caption and hashtags with `yt-dlp --print description " + (link || "<original URL>") + "` (or read them off the post). Repurpose it for TextInk (textink.ca) changing as little as possible: keep the wording, line breaks, emojis, tone and hashtag order. Change only what has to change: the old brand's name, @handles, links and brand hashtags become TextInk, @textink / textink.ca and #textink, and any claim that isn't true of TextInk gets the nearest true version. " + (language ? "If the original isn't in " + language + ", carry it into " + language + " with the same length and feel; keep hashtags that work in any language and swap the others for their common " + language + " equivalents. " : "") + "Show me the original and the new caption side by side with each change listed, then use the new caption for this reel.",
        "7. Export the reel and show me the contact sheet, the caption and the file path.",
        "",
        "Do not post or schedule anything. When it's done, ask me before replacing the placeholder in the " + slot + " slot.");
    return lines.join("\n");
}
