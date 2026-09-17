# RuffnecKk MapSense map generator

This directory governs the source used to build the packaged
`RuffnecKkMapSenseMapgen.exe`. The helper is launched hidden and below normal
priority by MapSense; players do not launch it themselves.

The [GPS adapter, tests and admission probe](../../../../Diablo/plugin-dev/mapsense/notes/GPS-PROOF.md) cover walking and
Teleport from MapSense 1.0.2 r3, including player clearance, gated traces and
effective mod inputs. Run `zig build gps-test` and `zig build gps-proof` for the
offline checks. The proof executables remain excluded from installation. The
regular helper now exposes the separately gated `route-binary` command described
below; the DLL does not request or render those routes yet.

The [read-only D2R comparison preparation](../../../../Diablo/plugin-dev/mapsense/notes/GPS-RUNTIME.md) adds a separate
`zig build gps-export` target and a governed external capture/comparison tool.
Its generated fixtures and synthetic tests do not establish live GPS behavior.

## Reproducible dependency

1. Clone `https://github.com/jaenster/libd2.git` into `vendor/libd2`.
2. Check out commit `ac4d735e57fcab6a3c356f810bb256da95a93716`.
3. From that checkout, apply `../../libd2-mapsense.patch`.
4. Build with Zig 0.16.0:

   ```powershell
   zig build --seed 0 -Doptimize=ReleaseSafe
   ```

The build script fixes the public target to `x86_64-windows-gnu` with Zig's
`baseline` CPU model. It intentionally does not expose native CPU selection:
release helpers must not inherit AVX, AVX2, AVX-512, or another optional ISA
extension from the workstation that builds them. Release output is stripped,
uses no linker-selected build ID, and is built with seed `0`; two clean builds
with independent caches and the same output path must therefore be
byte-identical.

The patch is the complete diff used by this candidate. It adds the immutable
label/waypoint and automap-geometry surfaces consumed by MapSense; it does not
add any live D2R memory access. Outdoor automap geometry follows each room's
exact DT1 mask, waypoint/shrine/terrain LvlSub seed order, and near-room seam
ownership, then stops before collision rasterization. Protocol MS1 v2 emits
one collision-proven, player-width opening for each directed outdoor level
pair and refuses to invent an averaged seam anchor. The offline GPS extensions
add explicit footprint/trace options and effective-data metadata to the existing
navigation primitives. The historical 1.0.2 r3 helper remains the comparison
baseline; adding the `route-binary` command intentionally changes the new helper.

## Active-mod inputs

The helper embeds its pinned vanilla dataset as the fallback. MapSense may
overlay the active mod by appending repeatable roots to any `labels`,
`geometry`, `geometry-binary`, or `route-binary` command:

```powershell
RuffnecKkMapSenseMapgen.exe labels 1337 2 4 109 `
  --excel-root "<mod>\data\global\excel" `
  --tiles-root "<mod>\data\global\tiles"
```

The Excel overlay covers `Levels`, `LvlPrest`, `LvlTypes`, `LvlMaze`,
`LvlSub`, `LvlWarp`, and `Objects`. DS1 files are resolved from the ordered
active tile roots first, then from the embedded vanilla assets. Conflicting
duplicate overrides, unsafe paths, malformed tables, and unresolved topology
fail closed. Level and preset enums are non-exhaustive, so an otherwise valid
custom numeric ID is generated without a per-mod code change.

The DLL passes these roots automatically when it starts the helper. This is a
developer interface; players still launch only D2R.

## Current-level GPS route protocol

`route-binary` takes the exact seed, difficulty, LevelId, world-subtile start,
world-subtile destination, `walk` or `teleport`, and a new output path. It loads
the effective mod tables and tiles, refuses cross-level routes, keeps the player
origin exact, and resolves a blocked map-derived destination to a standable
approach within 48 subtiles. It then writes MSR1 v1. Walking uses the small-player
footprint; Teleport preserves walking legs, marks cast landings explicitly,
applies the qualified gated trace, and refuses unqualified pad transitions.

MSR1 v1 has a fixed 52-byte little-endian header: magic/version/mode, seed,
difficulty, LevelId, exact start and destination, effective-data fingerprint,
move count, and zeroed reserved fields. Each 12-byte move contains world-subtile
X/Y, a `walk` or `teleport` kind, and three zeroed reserved bytes. The C++ reader
requires exact request identity and artifact size, an exact first move, and a
last move inside the same 48-subtile destination bound. It rejects pad kinds and
rejects a cast in a walking response. The limit is 65,536 moves and 1 MiB.
Session generation, destination revision and cancellation remain DLL-side
publication gates and are not delegated to this file format.

Binary geometry protocol MSA1 v3 records an explicit scope in addition to the
native tile-array provenance. Scope `0` emits the reusable standard campaign
for an act; a positive scope emits exactly the requested custom LevelId. This
prevents two unrelated levels that reuse one `Levels.Layer` from sharing reveal
completion. The `wallTree` and `raised` bits remain separate because D2R
chooses the floor/wall owner tree from the source tile array, not from
`orientation >= 0x10`.

The packaged executable is a generated release artifact. Its hash must match
the value recorded in the MapSense mission before deployment or publication.
Run `verify-labels.ps1` against the built executable to reject any VEX/EVEX
instruction and then exercise deterministic MS1 and MSA1 v3 output, all five
acts, exact waypoint coverage, valid floor/wall
provenance, unique physical seams and reciprocal one-subtile adjacency across
four governed seeds. The matrix also
pins the observed Spider Forest/Flayer Jungle crossing for seed `1395822899`
at `(5000, 4268)` / `(4999, 4268)` so the former averaged-label regression
cannot silently return.

Pass `-ActiveDataRoot <mod-data-root>` to add two mod-awareness gates. The
first requires the active dataset's real source-to-custom entrance and exact
waypoint coverage. The second rewrites that custom target to arbitrary
LevelId `733` in a temporary fixture and requires the same source coordinate
plus an exact-scope geometry artifact containing only level `733`, proving that
neither labels nor terrain depend on a BKVince or Rift identifier hard-coded in
the helper.
