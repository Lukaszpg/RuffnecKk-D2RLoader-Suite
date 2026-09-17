//! RuffnecKk MapSense 1.0.2 GPS admission probe, never part of the installed helper.
//! Generated terrain and this adapted movement model are NOT live D2R evidence.
//! The independent raw-cell audit keeps routing success distinct from model validity.
const std = @import("std");
const drlg = @import("d2-drlg");
const pf = @import("d2-pathfinding");
const gps = @import("gps_routing.zig");
const helper = @import("main.zig");

const seeds = [_]u32{ 1, 1337, 1395822899, 0x13572468 };
const fixture_levels = [_][]const i32{
    &.{ 2, 28, 29, 35, 37 }, // outdoor, Barracks, Jail, Catacombs
    &.{ 43, 62, 63, 64, 73, 74 }, // desert, Maggot Lair, Duriel, Arcane
    &.{ 76, 77, 78, 100, 101, 102 }, // jungle variants and Durance
    &.{ 104, 106, 108 },
    &.{ 110, 113, 118, 129, 131 },
};
const walk_mask: u16 = 0x1c09;
// Also witnessed in D2R's location-command validator. This server bound does
// not establish the player's viewport, skill availability or actual cast reach.
const reference_cast_range: i32 = 50;

const Counts = struct {
    routes: usize = 0,
    errors: usize = 0,
    cells: usize = 0,
    casts: usize = 0,
    gated_casts: usize = 0,
    pads: usize = 0,
    corner_candidates: usize = 0,
    cross_footprint_candidates: usize = 0,
};

fn ticks() u64 {
    var value: std.os.windows.LARGE_INTEGER = undefined;
    std.debug.assert(std.os.windows.ntdll.RtlQueryPerformanceCounter(&value).toBool());
    return @bitCast(value);
}

fn milliseconds(start: u64) f64 {
    var frequency: std.os.windows.LARGE_INTEGER = undefined;
    std.debug.assert(std.os.windows.ntdll.RtlQueryPerformanceFrequency(&frequency).toBool());
    return @as(f64, @floatFromInt(ticks() - start)) * 1000.0 /
        @as(f64, @floatFromInt(@as(u64, @bitCast(frequency))));
}

fn rawOpen(lv: *const pf.Level, x: i32, y: i32, mask: u16) bool {
    if (x < 0 or y < 0 or x >= lv.w or y >= lv.h) return false;
    const index = @as(usize, @intCast(y)) * @as(usize, @intCast(lv.w)) + @as(usize, @intCast(x));
    return (lv.cells[index] & mask) == 0;
}

fn crossOpen(lv: *const pf.Level, x: i32, y: i32, mask: u16) bool {
    return rawOpen(lv, x, y, mask) and rawOpen(lv, x - 1, y, mask) and
        rawOpen(lv, x + 1, y, mask) and rawOpen(lv, x, y - 1, mask) and
        rawOpen(lv, x, y + 1, mask);
}

fn observeCell(lv: *const pf.Level, x: i32, y: i32, mask: u16, c: *Counts) !void {
    if (!rawOpen(lv, x, y, mask)) return error.BlockedRouteCell;
    c.cells += 1;
    // D2R's size-two placement check uses this cross with 0x1c09. The
    // additional gated-level 0x804 rule is a separate coordinate trace, NOT
    // an extra footprint mask. The oracle below checks flat-grid stepping;
    // native room-border traversal remains unqualified.
    if (!crossOpen(lv, x, y, walk_mask)) {
        c.cross_footprint_candidates += 1;
    }
}

// Independent closed-form raster oracle: the minor coordinate is floor(i*d/D).
// This does not call libd2's incremental trace or its endpoint-adjustment helper.
fn gatedTraceClear(lv: *const pf.Level, from: pf.Point, to: pf.Point) bool {
    const dx: i32 = @intCast(@abs(to.x - from.x));
    const dy: i32 = @intCast(@abs(to.y - from.y));
    if (dx + dy < 4) return true;
    const sx: i32 = if (to.x > from.x) 1 else -1;
    const sy: i32 = if (to.y > from.y) 1 else -1;
    const a: pf.Point = .{ .x = from.x + (if (dx >= dy) sx * 2 else 0), .y = from.y + (if (dy >= dx) sy * 2 else 0) };
    const b: pf.Point = .{ .x = to.x - (if (dx >= dy) sx * 2 else 0), .y = to.y - (if (dy >= dx) sy * 2 else 0) };
    const rx: i32 = @intCast(@abs(b.x - a.x));
    const ry: i32 = @intCast(@abs(b.y - a.y));
    const n = @max(rx, ry);
    if (n == 0) return rawOpen(lv, a.x, a.y, 0x804);
    var i: i32 = 0;
    while (i <= n) : (i += 1) {
        const x = a.x + std.math.sign(b.x - a.x) * @divTrunc(i * rx, n);
        const y = a.y + std.math.sign(b.y - a.y) * @divTrunc(i * ry, n);
        if (!rawOpen(lv, x, y, 0x804)) return false;
    }
    return true;
}

fn audit(lv: *const pf.Level, route: *const pf.Route, a: pf.Point, b: pf.Point, tele: bool) !Counts {
    var c: Counts = .{};
    if (route.legs.len != 1 or route.legs[0].level != lv.id or route.legs[0].exit != null)
        return error.UnexpectedLevelTransition;
    const moves = route.legs[0].moves;
    if (moves.len == 0) return error.EmptyRoute;
    if (moves[0].x != a.x or moves[0].y != a.y or
        moves[moves.len - 1].x != b.x or moves[moves.len - 1].y != b.y)
        return error.ExactEndpointChanged;
    try observeCell(lv, moves[0].x, moves[0].y, walk_mask, &c);
    for (moves[1..], 1..) |m, i| {
        const p = moves[i - 1];
        const dx = m.x - p.x;
        const dy = m.y - p.y;
        switch (m.kind) {
            .teleport => {
                if (!tele or lv.teleport == .forbidden) return error.ForbiddenTeleport;
                if (@abs(dx) > reference_cast_range or @abs(dy) > reference_cast_range)
                    return error.ReferenceCastRangeExceeded;
                const source_room = lv.rooms.atSubtile(p.x, p.y) orelse return error.NoSourceRoom;
                const target_room = lv.rooms.atSubtile(m.x, m.y) orelse return error.NoLandingRoom;
                if (!lv.rooms.canTeleportBetween(source_room, target_room)) return error.NonAdjacentLandingRoom;
                if (lv.teleport == .gated and !gatedTraceClear(lv, .{ .x = p.x, .y = p.y }, .{ .x = m.x, .y = m.y }))
                    return error.GatedTraceBlocked;
                try observeCell(lv, m.x, m.y, walk_mask, &c);
                c.casts += 1;
                if (lv.teleport == .gated) c.gated_casts += 1;
            },
            .pad => {
                // Paired pad models need their own D2R authority proof. Do not
                // mistake an Arcane pad jump for an ordinary walk segment.
                if (lv.pads.len == 0) return error.UnmodelledPad;
                try observeCell(lv, m.x, m.y, walk_mask, &c);
                c.pads += 1;
            },
            .walk => {
                if (dx != 0 and dy != 0 and @abs(dx) != @abs(dy)) return error.NonOctileWalkSegment;
                const sx: i32 = std.math.sign(dx);
                const sy: i32 = std.math.sign(dy);
                const n = @max(@abs(dx), @abs(dy));
                var x = p.x;
                var y = p.y;
                for (0..n) |_| {
                    if (sx != 0 and sy != 0 and
                        (!rawOpen(lv, x + sx, y, walk_mask) or !rawOpen(lv, x, y + sy, walk_mask)))
                        c.corner_candidates += 1;
                    x += sx;
                    y += sy;
                    try observeCell(lv, x, y, walk_mask, &c);
                }
            },
        }
    }
    return c;
}

// Deliberately synthetic endpoints, selected in the largest generated component.
// They exercise long traversals, not the existing MapSense destination resolver.
fn endpoints(alloc: std.mem.Allocator, router: *pf.Router, lv: *pf.Level) ![2]pf.Point {
    const pm = try (try router.navFor(lv)).passMap(walk_mask);
    const components = try pm.components(alloc);
    if (pm.comp_count == 0) return error.NoPassableComponent;
    const counts = try alloc.alloc(u32, pm.comp_count + 1);
    defer alloc.free(counts);
    @memset(counts, 0);
    for (components) |c| counts[c] += 1;
    var largest: u32 = 1;
    for (counts[1..], 1..) |n, id| {
        if (n > counts[largest]) largest = @intCast(id);
    }
    var first: ?usize = null;
    var last: usize = 0;
    for (components, 0..) |c, i| {
        if (c != largest) continue;
        const x: i32 = @intCast(i % @as(usize, @intCast(lv.w)));
        const y: i32 = @intCast(i / @as(usize, @intCast(lv.w)));
        // Both inputs already fit the player-size candidate, so a clearance
        // failure describes the produced route rather than an impossible start.
        if (!crossOpen(lv, x, y, walk_mask)) continue;
        if (first == null) first = i;
        last = i;
    }
    const f = first orelse return error.NoPassableComponent;
    if (f == last) return error.TrivialComponent;
    const w: usize = @intCast(lv.w);
    return .{
        .{ .x = @intCast(f % w), .y = @intCast(f / w) },
        .{ .x = @intCast(last % w), .y = @intCast(last / w) },
    };
}

pub fn main(init: std.process.Init.Minimal) !void {
    var allocator_state: std.heap.DebugAllocator(.{}) = .init;
    defer std.debug.assert(allocator_state.deinit() == .ok);
    const alloc = allocator_state.allocator();
    var args = try std.process.Args.Iterator.initAllocator(init.args, alloc);
    defer args.deinit();
    _ = args.next();
    const seed_filter: ?u32 = if (args.next()) |v| (if (std.mem.eql(u8, v, "all")) null else try std.fmt.parseInt(u32, v, 0)) else null;
    const difficulty_filter: ?u8 = if (args.next()) |v| (if (std.mem.eql(u8, v, "all")) null else try std.fmt.parseInt(u8, v, 10)) else null;
    if (difficulty_filter != null and difficulty_filter.? > 2) return error.InvalidDifficulty;
    const data_options = try helper.DataOptions.parse(&args);
    var inputs = try helper.LoadedInputs.load(alloc, data_options);
    defer inputs.deinit(alloc);
    var totals: Counts = .{};
    var fixtures: usize = 0;
    std.debug.print("{{\"kind\":\"scope\",\"base\":\"MapSense 1.0.2 r3\",\"profile\":\"player-cross-and-gated-trace\",\"activeExcelRoots\":{d},\"inputFingerprint\":{d},\"d2rRuntimeCompared\":false,\"nativeFootprintQualified\":false}}\n", .{ data_options.excel_root_count, inputs.fingerprint });
    for (seeds) |seed| {
        if (seed_filter != null and seed != seed_filter.?) continue;
        for (0..3) |difficulty| {
            if (difficulty_filter != null and difficulty != difficulty_filter.?) continue;
            for (fixture_levels, 0..) |levels, act| {
                const environment = try gps.Environment.create(alloc, seed, @enumFromInt(difficulty), inputs.tables(data_options));
                defer environment.destroy();
                const world = &environment.world;
                const router = &environment.router;
                const generation_start = ticks();
                try environment.loadAct(@intCast(act));
                std.debug.print("{{\"kind\":\"generation\",\"seed\":{d},\"difficulty\":{d},\"act\":{d},\"milliseconds\":{d:.3}}}\n", .{ seed, difficulty, act + 1, milliseconds(generation_start) });
                for (levels) |id| {
                    const lv = world.level(id) orelse return error.MissingFixtureLevel;
                    const ends = try endpoints(alloc, router, lv);
                    fixtures += 1;
                    for ([_]bool{ false, true }) |tele| {
                        const start = ticks();
                        var route = gps.route(
                            router,
                            .{ .level = id, .x = ends[0].x, .y = ends[0].y },
                            .{ .level = id, .x = ends[1].x, .y = ends[1].y },
                            if (tele) .teleport else .walk,
                        ) catch |err| {
                            totals.errors += 1;
                            std.debug.print("{{\"kind\":\"route-error\",\"seed\":{d},\"difficulty\":{d},\"level\":{d},\"teleport\":{},\"error\":\"{s}\"}}\n", .{ seed, difficulty, id, tele, @errorName(err) });
                            continue;
                        };
                        defer route.deinit();
                        const elapsed = milliseconds(start);
                        const c = audit(lv, &route, ends[0], ends[1], tele) catch |err| {
                            totals.errors += 1;
                            std.debug.print("{{\"kind\":\"audit-error\",\"seed\":{d},\"difficulty\":{d},\"level\":{d},\"teleport\":{},\"error\":\"{s}\"}}\n", .{ seed, difficulty, id, tele, @errorName(err) });
                            continue;
                        };
                        totals.routes += 1;
                        totals.casts += c.casts;
                        totals.gated_casts += c.gated_casts;
                        totals.cells += c.cells;
                        totals.pads += c.pads;
                        totals.corner_candidates += c.corner_candidates;
                        totals.cross_footprint_candidates += c.cross_footprint_candidates;
                        std.debug.print("{{\"kind\":\"route\",\"seed\":{d},\"difficulty\":{d},\"level\":{d},\"teleport\":{},\"milliseconds\":{d:.3},\"moves\":{d},\"cells\":{d},\"casts\":{d},\"gatedCasts\":{d},\"pads\":{d},\"cornerCandidates\":{d},\"crossFootprintCandidates\":{d}}}\n", .{ seed, difficulty, id, tele, elapsed, route.moveCount(), c.cells, c.casts, c.gated_casts, c.pads, c.corner_candidates, c.cross_footprint_candidates });
                    }
                }
            }
        }
    }
    std.debug.print("{{\"kind\":\"summary\",\"fixtures\":{d},\"routes\":{d},\"errors\":{d},\"cells\":{d},\"casts\":{d},\"gatedCasts\":{d},\"pads\":{d},\"cornerCandidates\":{d},\"crossFootprintCandidates\":{d},\"gpsAdmitted\":false,\"d2rRuntimeCompared\":false}}\n", .{ fixtures, totals.routes, totals.errors, totals.cells, totals.casts, totals.gated_casts, totals.pads, totals.corner_candidates, totals.cross_footprint_candidates });
    if (fixtures == 0 or totals.routes == 0 or totals.casts == 0) return error.EmptyCoverage;
    if (totals.errors != 0) return error.ModelQualificationFailed;
    if (totals.cross_footprint_candidates != 0) return error.PlayerClearanceFailed;
}
