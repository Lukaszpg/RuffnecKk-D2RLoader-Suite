const std = @import("std");

pub fn build(b: *std.Build) void {
    // This executable is a public runtime companion, not a host-optimized
    // developer tool. Keep the target immutable so a release built on a newer
    // workstation cannot silently inherit AVX, AVX2, or AVX-512 instructions.
    const target = b.resolveTargetQuery(.{
        .cpu_arch = .x86_64,
        .cpu_model = .baseline,
        .os_tag = .windows,
        .abi = .gnu,
    });
    const optimize = b.standardOptimizeOption(.{});
    const libd2 = b.dependency("libd2", .{
        .target = target,
        .optimize = optimize,
    });

    const exe = b.addExecutable(.{
        .name = "RuffnecKkMapSenseMapgen",
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/main.zig"),
            .target = target,
            .optimize = optimize,
            // Strip host/cache path metadata so fixed-seed release builds are
            // byte-identical even when their Zig caches are independent.
            .strip = true,
        }),
    });
    // A random build ID changes the PE timestamp and `.buildid` section even
    // when every source input is identical. Release hashes must be byte-exact
    // across clean builds, so omit that non-functional identifier.
    exe.build_id = .none;
    exe.root_module.addImport("d2-drlg", libd2.module("d2-drlg"));
    exe.root_module.addImport("d2-pathfinding", libd2.module("d2-pathfinding"));
    exe.root_module.addImport("d2-render", libd2.module("d2-render"));
    b.installArtifact(exe);

    // Offline GPS evidence only. This step is not part of the installed helper
    // and does not enable routing in the MapSense 1.0.2 runtime.
    const gps_proof = b.addExecutable(.{
        .name = "RuffnecKkMapSenseGpsProof",
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/gps_proof.zig"),
            .target = target,
            .optimize = optimize,
        }),
    });
    gps_proof.root_module.addImport("d2-drlg", libd2.module("d2-drlg"));
    gps_proof.root_module.addImport("d2-pathfinding", libd2.module("d2-pathfinding"));
    gps_proof.root_module.addImport("d2-render", libd2.module("d2-render"));
    const run_gps_proof = b.addRunArtifact(gps_proof);
    if (b.args) |args| run_gps_proof.addArgs(args);
    b.step("gps-proof", "Audit walking and teleport routes offline; no D2R compatibility claim")
        .dependOn(&run_gps_proof.step);

    const gps_export = b.addExecutable(.{
        .name = "RuffnecKkMapSenseGpsExport",
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/gps_export.zig"),
            .target = target,
            .optimize = optimize,
        }),
    });
    gps_export.root_module.addImport("d2-drlg", libd2.module("d2-drlg"));
    gps_export.root_module.addImport("d2-pathfinding", libd2.module("d2-pathfinding"));
    gps_export.root_module.addImport("d2-render", libd2.module("d2-render"));
    const run_gps_export = b.addRunArtifact(gps_export);
    if (b.args) |args| run_gps_export.addArgs(args);
    b.step("gps-export", "Export one generated level for offline collision comparison; never installed")
        .dependOn(&run_gps_export.step);

    const gps_tests = b.addTest(.{
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/gps_routing_tests.zig"),
            .target = target,
            .optimize = optimize,
        }),
    });
    gps_tests.root_module.addImport("d2-drlg", libd2.module("d2-drlg"));
    gps_tests.root_module.addImport("d2-pathfinding", libd2.module("d2-pathfinding"));
    gps_tests.root_module.addImport("d2-data", libd2.module("d2-data"));
    b.step("gps-test", "Test MapSense GPS clearance, gated casts and effective data")
        .dependOn(&b.addRunArtifact(gps_tests).step);
}
