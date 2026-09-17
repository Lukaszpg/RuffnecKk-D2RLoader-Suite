//! Export generated collision and rooms for the external, read-only D2R comparison.
//! Deliberately excluded from the installed MapSense 1.0.2 r3 helper.
const std = @import("std");
const pf = @import("d2-pathfinding");
const gps = @import("gps_routing.zig");
const helper = @import("main.zig");

const max_cells = 16_777_216;
const max_rooms = 4096;
const max_relations = 262_144;
const max_bytes = 80 * 1024 * 1024;

fn integer(alloc: std.mem.Allocator, out: *std.ArrayList(u8), comptime T: type, value: T) !void {
    var bytes: [@sizeOf(T)]u8 = undefined;
    std.mem.writeInt(T, &bytes, value, .little);
    try out.appendSlice(alloc, &bytes);
}

fn exportGrid(alloc: std.mem.Allocator, lv: *const pf.Level, seed: u32, difficulty: u32, fingerprint: u64, output: []const u8) !void {
    const cells = std.math.cast(usize, @as(i64, lv.w) * lv.h) orelse return error.InvalidGrid;
    if (lv.w <= 0 or lv.h <= 0 or cells > max_cells or lv.cells.len != cells or
        lv.rooms.rooms.len == 0 or lv.rooms.rooms.len > max_rooms or lv.origin_x < 0 or lv.origin_y < 0)
        return error.InvalidGrid;
    const ox = std.math.cast(i32, @as(i64, lv.origin_x) * 5) orelse return error.InvalidGrid;
    const oy = std.math.cast(i32, @as(i64, lv.origin_y) * 5) orelse return error.InvalidGrid;
    if (@as(i64, ox) + lv.w > std.math.maxInt(i32) or @as(i64, oy) + lv.h > std.math.maxInt(i32))
        return error.InvalidGrid;
    var size: usize = 48 + cells * 2;
    var relations: usize = 0;
    for (lv.rooms.rooms, 0..) |_, i| relations += lv.rooms.nearOf(@intCast(i)).len;
    if (relations > max_relations) return error.TooManyNeighbours;
    size += 20 * lv.rooms.rooms.len + 4 * relations;
    if (size > max_bytes) return error.ArtifactTooLarge;
    var out: std.ArrayList(u8) = .empty;
    defer out.deinit(alloc);
    try out.ensureTotalCapacity(alloc, size);
    try out.appendSlice(alloc, "MSGC");
    try integer(alloc, &out, u32, 1);
    try integer(alloc, &out, u32, seed);
    try integer(alloc, &out, u32, difficulty);
    try integer(alloc, &out, i32, lv.id);
    try integer(alloc, &out, i32, ox);
    try integer(alloc, &out, i32, oy);
    try integer(alloc, &out, u32, @intCast(lv.w));
    try integer(alloc, &out, u32, @intCast(lv.h));
    try integer(alloc, &out, u32, @intCast(lv.rooms.rooms.len));
    try integer(alloc, &out, u64, fingerprint);
    for (lv.rooms.rooms, 0..) |room, i| {
        if (room.x < 0 or room.y < 0 or room.w <= 0 or room.h <= 0 or
            (@as(i64, room.x) + room.w) * 5 > lv.w or (@as(i64, room.y) + room.h) * 5 > lv.h)
            return error.InvalidRoom;
        try integer(alloc, &out, i32, ox + room.x * 5);
        try integer(alloc, &out, i32, oy + room.y * 5);
        try integer(alloc, &out, i32, room.w * 5);
        try integer(alloc, &out, i32, room.h * 5);
        const near = lv.rooms.nearOf(@intCast(i));
        if (near.len > lv.rooms.rooms.len) return error.InvalidNeighbours;
        try integer(alloc, &out, u32, @intCast(near.len));
        for (near) |index| {
            if (index >= lv.rooms.rooms.len) return error.InvalidNeighbours;
            try integer(alloc, &out, u32, index);
        }
    }
    for (lv.cells) |cell| try integer(alloc, &out, u16, cell);
    if (out.items.len != size) return error.ArtifactSizeMismatch;
    var threaded = std.Io.Threaded.init_single_threaded;
    const io = threaded.io();
    const file = try std.Io.Dir.cwd().createFile(io, output, .{ .exclusive = true });
    defer file.close(io);
    try file.writeStreamingAll(io, out.items);
    std.debug.print("{{\"kind\":\"gps-generated-grid\",\"seed\":{d},\"difficulty\":{d},\"levelId\":{d},\"rooms\":{d},\"cells\":{d},\"bytes\":{d},\"inputFingerprint\":{d},\"d2rRuntimeCompared\":false}}\n", .{ seed, difficulty, lv.id, lv.rooms.rooms.len, cells, size, fingerprint });
}

pub fn main(init: std.process.Init.Minimal) !void {
    const alloc = std.heap.page_allocator;
    var args = try std.process.Args.Iterator.initAllocator(init.args, alloc);
    defer args.deinit();
    _ = args.next();
    const seed = try std.fmt.parseInt(u32, args.next() orelse return error.MissingSeed, 0);
    const difficulty = try std.fmt.parseInt(u32, args.next() orelse return error.MissingDifficulty, 10);
    const level_id = try std.fmt.parseInt(i32, args.next() orelse return error.MissingLevel, 10);
    const output = args.next() orelse return error.MissingOutput;
    if (difficulty > 2 or level_id <= 0) return error.InvalidSession;
    const options = try helper.DataOptions.parse(&args);
    var inputs = try helper.LoadedInputs.load(alloc, options);
    defer inputs.deinit(alloc);
    // The active table owns level IDs; never select the act by vanilla ID ranges.
    for (0..5) |act| {
        const environment = try gps.Environment.create(alloc, seed, @enumFromInt(difficulty), inputs.tables(options));
        defer environment.destroy();
        try environment.loadAct(@intCast(act));
        if (environment.world.level(level_id)) |level| {
            try exportGrid(alloc, level, seed, difficulty, inputs.fingerprint, output);
            return;
        }
    }
    return error.LevelNotGenerated;
}
