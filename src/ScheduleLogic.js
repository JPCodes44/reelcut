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
function slots(day, startMinute, spacingMinutes) {
    var base = new Date(dayStart(day));
    if (!isFinite(base.getTime()) || !isFinite(startMinute) || !isFinite(spacingMinutes)
            || Math.floor(startMinute) !== startMinute || Math.floor(spacingMinutes) !== spacingMinutes
            || startMinute < 0 || [60,90,120].indexOf(spacingMinutes) < 0
            || startMinute + (targetCount - 1) * spacingMinutes >= 1440) return [];
    var out = [];
    for (var i = 0; i < targetCount; ++i) {
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
    var summary = {covered:0, scheduled:0, published:0, failed:0, awaiting:0, target:targetCount};
    (cards || []).forEach(function(p) {
        if (p.platform !== platform) return;
        if (p.status === "failed") summary.failed++;
        else if (p.status === "published") { summary.published++; summary.covered++; }
        else if (p.status === "awaiting") { summary.awaiting++; summary.covered++; }
        else if (p.status === "scheduled" || p.status === "publishing") { summary.scheduled++; summary.covered++; }
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
