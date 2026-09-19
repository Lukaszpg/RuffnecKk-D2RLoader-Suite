# Damage cleanup ABI v1 — isolated 1.0.1 candidate

This work was authorized on 2026-09-05 for DollExplosion compatibility.
It does not change the current Suite release scope. The default build remains
CastTriggers 1.0.0. Enable the CMake option
`RUFFNECKK_CAST_TRIGGERS_DAMAGE_CLEANUP_V1=ON` to build the 1.0.1 candidate;
that target is explicitly not public-archive eligible. This is a temporary
incubation build gate, not a player setting or a promise to ship two variants.

The canonical public header is `src/damage_cleanup_api.hpp`. DollExplosion
carries a byte-identical consumer copy. There is no import-library dependency,
new D2RLoader service, new native hook, or change to Yinyin's Bind And Summon.

## Contract

Export `RuffnecKkCastTriggersGetDamageCleanupApi(version, size)` returns
API v1 only for the exact negotiated table size. The table also identifies
the governed 0x180-byte D2Damage layout. No C++ objects or CRT ownership cross
the DLL boundary. The request supplies the native destructor entry address and
its original 32 expected bytes. The provider checks both against the surface
it exclusively hooked after its own full native fingerprint passed.

The provider captures all 32 live entry bytes immediately after successful
hook installation. Every acceptance and execution rechecks that exact snapshot;
an additional hook, modified tail, mismatched entry, changed expected bytes,
inactive provider, invalid version/size/layout, or nonzero reserved field
rejects before invoking client code.

`run(request, damage, operation, userData)` synchronously invokes the
client operation then `HookDestroyDamage`, which removes CastTriggers'
critical marker and calls its original destructor exactly once. The record
must be at least 0x180 bytes, 16-byte aligned, fully initialized by the caller,
and compatible with the layout token. Neither the callback nor caller may
destroy it separately, retain it after return, or initiate plugin load/unload
inside the callback. Native SEH faults trigger one cleanup attempt; a cleanup
fault is reported without retry. Corrupted native state is not claimed to be
recoverable merely because an exception was caught.

Results have distinct ownership semantics:

- `Rejected`: neither operation nor cleanup was called.
- `Completed`: operation succeeded and cleanup completed once.
- `OperationFailed`: operation returned false and cleanup completed once.
- `Fault`: an operation or cleanup fault occurred; never retry the record.

A recursive lock admits nested deaths on the same thread while serializing
external operations. Shutdown stops admission and waits for admitted work
before D2RLoader removes hooks. Consumers retain a Windows module reference
across the call, but do not cache a trampoline or use module retention as a
substitute for this logical shutdown protocol. DLL lifecycle changes are
performed through D2RLoader, outside operation callbacks.

## Consumer routing

DollExplosion checks the native destructor anew before each explosion. If its
original bytes are intact, the native standalone path remains available.
Otherwise it resolves the canonical or legacy CastTriggers module, rejects
ambiguous modules, negotiates v1 and delegates the entire damage operation.
It never calls an arbitrary changed entry, chains a foreign trampoline, or
retries after a provider attempt. A provider loading after DollExplosion is
therefore discovered at use time. An old provider without this API is rejected
when its hook is present; removing its validation is not a supported fallback.

Actual plugin loading/unloading is still a runtime gate. The standalone path
does not authorize concurrent hot-installation of hooks during native damage
execution; normal loader startup/shutdown quiescence remains required.

## Evidence and gates

- MSVC 19.44 Release x64, warnings-as-errors: default and candidate build.
- CTest: candidate 3/3; default 2/2. Policy assertions are enabled in Release.
- Tests cover exact ABI/request validation, each snapshot byte mutation,
  inactive/restarted provider, operation failure, operation/cleanup SEH faults,
  nested execution, shutdown waiting for cleanup, and actual candidate-DLL
  export negotiation without a game or hook.
- Candidate two clean builds: SHA-256
  `E820C4A0887375CC8F4EC300272446F9FE41D08812E7871304B784B6DF69C005`.
- Default build before/after source changes: SHA-256
  `FCC8993377349C8FC4B7CE8C7C48C78092F39E19E7A800FA611B1AEF2E7ED343`.
  This proves preservation of this locally reproduced default artifact, not
  equivalence with an independently built or deployed release artifact.
- Public API header SHA-256:
  `270AF344B53A23345D754A522B4550844CD3B4733FEDE751B9BB17DC66BCA4CB`.
- Full-stack cold start, both actual load orders, gameplay, native unload,
  global/mod-local scopes and network behavior: **not run** for this candidate.
- The global Suite source check currently reports out-of-scope MapSense nested
  build/configuration and Potion Auto Pick-up TOML-pin failures. No global
  Suite PASS or release qualification is claimed.

Do not change the current release plan or allowlist to include this candidate.
Rollback of the source feature is to keep the build option OFF. Any future
temporary runtime deployment must back up and restore the exact installed DLL,
preserve player TOMLs, retain a receipt and use explicit runtime authorization.
