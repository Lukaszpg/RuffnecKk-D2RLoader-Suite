# PlayerX Scaling Tweaks

PlayerX Scaling Tweaks is a configurable RuffnecKk D2RLoader plugin that keeps a
minimum difficulty baseline while allowing life, experience, monster offense
to use independent caps and optionally makes `/players X` count like X nearby
party members for NoDrop. Its optional Battle.net simulation disables
artificial player-count controls and uses connected players as the only
dynamic source.

Current status: **1.0.0 Suite 1.4 candidate. Runtime qualification and release
packaging are still required before publication.**

Version 1.0.0 adds `[battle-net-simulation]` and declares the API v3 shared
execution role required by its combined local-control and gameplay behavior.

## Default player experience

The included TOML uses these defaults:

| Setting | Default | Concrete effect |
|---|---:|---|
| Minimum scaling count | 1 | Monster scaling keeps its vanilla p1 baseline. |
| Maximum `/players` command | 8 | The command keeps its vanilla p8 ceiling. |
| Monster life cap | Unlimited | HP follows the effective player count. |
| Monster experience cap | Unlimited | XP follows the effective player count. |
| Monster offense cap | Unlimited | Physical damage and Attack Rating follow the native Nightmare/Hell factor. |
| NoDrop party simulation | Disabled | NoDrop keeps the native nearby-party formula. |
| Battle.net simulation | Disabled | `/players` and Offline Difficulty keep their native behavior. |

The configuration is
[`ruffneckk-playerx-scaling-tweaks.toml`](config/ruffneckk-playerx-scaling-tweaks.toml). Its comments
are the player-facing template and explain every value.

The strict configuration range is 1–65,535. Values above p8 are extended mod
values and remain runtime-unqualified until they are measured in game.

## Battle.net simulation

```toml
[battle-net-simulation]
enabled = true
```

When enabled, `/players` is unavailable, Offline Difficulty is reset and
locked to p1, and the artificial player-count value is ignored by gameplay.
Connected players become the only dynamic source. The configurable minimum
and independent channel caps still apply after that real count is obtained.

For Battle.net-style defaults, keep the minimum at 1, all channel caps at 0,
and NoDrop party simulation disabled. Battle.net simulation takes priority if
the NoDrop simulation option is also true.

The current native evidence proves an immobile p1 control, not whether the
slider is visually greyed or hidden.

## NoDrop nearby-party simulation

D2R combines the two inputs as:

```text
effective NoDrop players = nearby + (players command - nearby) / 2
```

The division uses integer truncation. With one nearby member (the player),
`/players 4` therefore produces an effective NoDrop count of 2. Four living
party members in the same area produce an effective count of 4.

The optional mode makes the accepted `/players` count act as the nearby-party
source for this calculation:

```toml
[no-drop]
players-command-simulates-nearby-party = true
```

With that setting, solo `/players 4` produces an effective NoDrop count of 4
and solo `/players 8` produces 8. A larger real nearby-party count is never
reduced. PlayerX Scaling Tweaks also prevents the monster's persistent player-count
stat from lowering the simulated result; the native probability calculation
itself remains intact.

## Scaling switches

Setting a monster channel's `enabled` value to `false` does not erase the
baseline. It freezes that channel at
`player-count.minimum-scaling-players`. A `maximum-players` value of `0` means
unlimited scaling. Every non-zero maximum must be at least the configured
baseline: with a p4 baseline, `4` freezes the channel at p4, `5` or higher caps
it above the baseline, and `1..3` is invalid.

D2R couples its player-count monster physical damage bonus with monster Attack
Rating in Nightmare and Hell. The `[monster-offense-scaling]` section controls
that whole native offense factor. Normal difficulty keeps its native rule.

`[no-drop].players-command-simulates-nearby-party = false` leaves the original
NoDrop inputs and formula untouched.

## Installation and ownership

The final plugin will support either of the normal D2RLoader scopes:

- global: `<D2R>/d2rloader/plugins/`;
- mod-local: `<D2R>/mods/<mod>/d2rloader/plugins/`.

Only one scope may load the DLL in a given D2R process. Separate local D2R
processes can each load it. Configuration lookup prefers the active mod, then
the plugin's scope, then the global config directory. A TOML that exists but is
invalid refuses the plugin instead of silently using other values.

PlayerX Scaling Tweaks must be the only owner of its native surfaces. Before runtime
deployment in BKVince:

- remove the old `ruffneckk-player-difficulty-overrides.json` patch;
- keep PluginPack `misc.playersCommandLimit` at 8;
- keep `misc.monsterHpPlayerCountCap` and
  `misc.monsterExperiencePlayerCountCap` at 0.

These values keep `plugin-misc.dll` and its unrelated features installed while
preventing its optional player-scaling hooks from competing with this plugin.

## Compatibility and rollback

The plugin makes no save-format changes. Removing the DLL and TOML restores
the native engine behavior, provided no retired overlapping patch is restored
at the same time.

Compatibility is decided only by the complete native byte fingerprint used by
the plugin. D2R build names and distribution channels are diagnostic, never an
allowlist. The current static evidence covers the governed native surface shared
by D2R 3.2.92777 and Battle.net 3.3.93847. Steam 3.3.93787 remains admissible
but unqualified until its byte-exact native evidence exists.

The plugin has a shared execution role. For TCP/IP, host and clients must use
the same plugin and configuration before compatibility can be claimed. A
client-side installation cannot change a remote host that does not run it.

## Credits

Created by **RuffnecKk**.

Thanks to **D2MOO** for the semantic reference used to understand the historical
monster offense and NoDrop algorithms. No D2MOO address, structure layout or
32-bit ABI is used in the D2R plugin.

The pinned eezstreet D2RL-Plugins implementation was also audited for the
existing `/players`, monster-life and monster-experience ownership surfaces.
No eezstreet DLL is modified, linked or redistributed.
