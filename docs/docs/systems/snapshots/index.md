# Snapshots

<span class="tag sim">SIM</span> SeinARTS captures the entire simulation state — entities, components, abilities, resolvers, PRNG, match flow, player state — into a portable `.seinsnapshot` blob. Same primitive powers save-game, future drop-in/drop-out catch-up, and (eventually) state-dump-on-desync.

## What it captures

A snapshot is a single `FSeinWorldSnapshot` USTRUCT serialized through `FObjectAndNameAsStringProxyArchive`. Contents:

| Bucket | Includes |
|---|---|
| **Header** | snapshot version, framework + game version strings, map identifier, capture timestamp |
| **Sim metadata** | current tick, session seed, deterministic PRNG state |
| **Match flow** | full `FSeinMatchSettings`, `ESeinMatchState`, match start tick, starting-state deadline |
| **Player + entity state** | per-player `FSeinPlayerState` map (resources, tags, archetype modifiers, stat counters), per-entity records (slot index + generation + transform + owner + alive flag + actor class path) |
| **Ability + resolver pools** | every slot in `USeinWorldSubsystem::AbilityPool` and `CommandBrokerResolverPool` — class path + UPROPERTY-tagged state bytes via `UClass::SerializeTaggedProperties`. Cooldowns, charges, internal counters all round-trip. |
| **Component storages** | one opaque blob per `UScriptStruct` with live components. Reflection-backed; no per-component code needed. |
| **Local camera state** | one `FSeinCameraSnapshotData` for the local PC's camera — see below. |

The capture is **save-game grade**, not approximate: an ability with 10 seconds left on a 30-second cooldown will land at 10 seconds on restore, not 0 and not 30. State on a turn boundary is bit-identical to the source sim.

## What it doesn't capture

Render-side state — actor positions, particles, audio cues, animation poses — is **not** in the snapshot. It rebuilds from the sim on restore: the actor bridge spawns an actor for every restored entity (using each record's `ActorClassPath`), reattaches it to the entity handle, and the normal visual-event pipeline plays out from the new sim state. Orphan actors (had an entity at capture, doesn't anymore) get culled.

## Console commands

Drive these from the in-PIE console (`~`).

| Command | What it does |
|---|---|
| `Sein.Net.DumpSnapshot [FileName]` | Capture sim state. Default filename: `Snapshot_<Map>_tick<N>.seinsnapshot`. Writes to `Saved/Snapshots/`. |
| `Sein.Net.LoadSnapshot <FileNameOrPath>` | Restore. Bare filenames resolve against `Saved/Snapshots/`; auto-appends `.seinsnapshot` if missing. **Standalone-only** (see below). |

The dumped file is plain binary serialized via the same proxy archive the replay writer uses, so it round-trips `TObjectPtr`, `FInstancedStruct`, and soft references cleanly.

## Networking caveat

`Sein.Net.LoadSnapshot` refuses to run when `NetMode != NM_Standalone`. A snapshot load is a single-peer state rewind: rolling THIS peer's sim back to tick T while every other peer keeps marching forward stalls the lockstep gate permanently — the local turn cursor goes backward but the network-layer turns dispatched 100..N never re-fire.

Multi-peer resync is the planned drop-in/drop-out catch-up RPC: server captures a snapshot, chunks it across reliable RPCs to the joiner, then replays the command tail until the joiner is at the live tick. That's a follow-up phase. For now, snapshot dump/load is for **save-game testing in standalone PIE**.

## Camera state

Snapshots include local camera pose so a save-load round-trip lands the player back at the exact view they captured from — same pivot, yaw, pitch, zoom. This is **local-only** state; in a multi-peer resync each peer keeps its own camera and ignores this field.

The framework doesn't hardcode "the camera is `ASeinCameraPawn`" — projects routinely swap in third-party Fab marketplace camera systems, custom C++ pawns, or pure-Blueprint pawns. Camera capture/restore goes through a small interface, `ISeinSnapshotCameraProvider`, and any UObject reachable from the local PC can implement it:

- `ASeinCameraPawn` (the framework's shipped pawn) implements it natively. Default behavior: location, yaw, pitch, and zoom round-trip. Snap is instantaneous on restore — no smooth-interp lerp from current state, that feels wrong for a save-load.
- Marketplace / custom C++ pawn — inherit from `ISeinSnapshotCameraProvider`, override the two `_Implementation` methods.
- Blueprint-only pawn — add the interface in **Class Settings → Interfaces**, implement `Capture Camera State` and `Restore Camera State` as event-graph nodes.
- Don't want camera in saves — don't implement the interface. The framework no-ops gracefully.

Lookup walks `PC → GetPawn() → GetViewTarget() → PC` and uses the first interface implementer found.

See **[Custom Camera Provider](custom-camera.md)** for the implementation guide.

## Architecture

```
USeinWorldSubsystem::CaptureSnapshot(out FSeinWorldSnapshot)
    └─ broadcasts OnCaptureSnapshotPostSim
         └─ USeinMatchBootstrapSubsystem (Framework module — bound at OnWorldBeginPlay)
              └─ Walks PC → pawn → view target → PC for ISeinSnapshotCameraProvider
              └─ ISeinSnapshotCameraProvider::Execute_CaptureCameraState(provider, &Snapshot.CameraState)

USeinWorldSubsystem::RestoreSnapshot(in FSeinWorldSnapshot)
    └─ broadcasts OnRestoreSnapshotPostSim (after sim restored + actor bridge reconciled)
         └─ USeinMatchBootstrapSubsystem
              └─ ISeinSnapshotCameraProvider::Execute_RestoreCameraState(provider, &Snapshot.CameraState)
```

The interface lives in `SeinARTSCoreEntity`, but the camera pawn lives in `SeinARTSFramework`. The cycle is broken with a multicast delegate: CoreEntity broadcasts; Framework's `USeinMatchBootstrapSubsystem` binds + does the interface lookup. Custom impls in your project's module bind through the same interface — no Framework-module dependency required.

## Plugin settings

None. Snapshot capture/restore has no tunables — the format is the format. Camera provider lookup is by interface, no class-path setting to wire up.

## Pages in this section

- [Custom Camera Provider](custom-camera.md) — implement `ISeinSnapshotCameraProvider` on a marketplace pawn, custom C++ pawn, or Blueprint-only pawn.

## Related console commands

The snapshot system reuses the deterministic state-hash machinery exposed by:

- `Sein.Net.DumpState` — print the current tick + state hash + entity count + session seed for the local sim. Run on every window in turn after a snapshot load — same Tick should produce the same StateHash on every machine if lockstep is healthy.
