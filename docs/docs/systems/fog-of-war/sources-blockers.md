# Vision Sources & Blockers

<span class="tag sim">SIM</span> Two ways an entity participates in fog-of-war: as a **source** (emits vision into nearby cells) or as a **blocker** (occludes vision passing through its cells). Most units are sources. Smoke grenades, hedgerows, destructibles-in-progress are blockers. Tanks are typically both (they see + their hull blocks line of sight).

## Sources — `FSeinVisionData`

`FSeinVisionData` is a sim component (lives in `SeinARTSCoreEntity/Public/Components/`). Add it to entities that emit vision. The default impl reads it via reflection-keyed component storage; the sim never references it by type.

```cpp
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct FSeinVisionData : public FSeinComponent
{
    /** Eye height for lampshade test (per-source, infantry ~180cm, tank ~250cm). */
    FFixedPoint EyeHeight;

    /** Stamp configs — one entity can emit multiple shaped stamps. */
    TArray<FSeinVisionStamp> Stamps;

    /** Bitmask of layers this entity is VISIBLE on. Observer needs at least
     *  one matching bit stamped at the entity's cell to see it. */
    uint8 EmissionLayerMask;
};
```

Each `FSeinVisionStamp` declares one perception cone:

- **Shape** — radial, rectangular, or cone (oriented by entity yaw + per-stamp yaw offset)
- **Geometry** — radius (for radial), extents (for rect), angle + range (for cone)
- **EmissionLayerMask** — which EVNNNNNN bits this stamp emits on (V for Normal, N0..N5 for custom layers)
- **bEnabled** — runtime toggle; disabled stamps don't stamp + don't refcount

Multiple stamps on one entity is normal: a tank with a forward 270° vision cone and a smaller rear 90° cone, a building with cones from each window, a Stealth detector with a wide Normal radial AND a narrower Stealth-only radial.

## How sources register

Automatic. `USeinFogOfWarDefault::TickStamps` walks every entity carrying `FSeinVisionData`:

1. For each new entity (no `FSeinFogSourceState` memo), creates a memo and stamps from scratch
2. For each existing entity, compares pose (`WorldPos + Rotation`) + `StampsHash` against the memo
3. Identical → skip (the perf win — most units don't cross cells per tick)
4. Changed → tear down the old footprint, re-stamp, write the new memo
5. For each entity that vanished (no longer in the pool), tear down its footprint

No explicit `RegisterSource` call needed. The impl auto-discovers + manages lifecycle.

The base abstract `USeinFogOfWar::RegisterSource` / `UpdateSource` / `UnregisterSource` virtuals exist for impls that prefer explicit lifecycle (e.g. an actor-bridge-driven version), but the default impl leaves them as no-ops and walks the pool itself.

## Blockers — runtime via `FSeinExtentsData`

`FSeinExtentsData` (in `SeinARTSCoreEntity/Public/Components/`) is the same component nav uses for blocker authoring. It carries an independent flag for fog:

```cpp
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct FSeinExtentsData : public FSeinComponent
{
    TArray<FSeinExtentsShape> Shapes;
    bool bBlocksNav = false;            // Independent
    bool bBlocksFogOfWar = false;       // Independent
    uint8 BlockedNavLayerMask = 0xFF;   // Nav-layer filter
    // Fog blockers use a per-shape EmissionLayerMask on the FSeinVisionStamp
    // (carried by the shape itself when used as a fog blocker)
    // ...
};
```

A vehicle with both flags set blocks nav AND blocks fog through its hull. A smoke grenade entity with only `bBlocksFogOfWar` blocks fog only. The two independent flags is the right model — projects rarely want them in lockstep.

## How blockers stamp

`USeinFogOfWarDefault::RebuildDynamicBlockers` runs at the top of `TickStamps`:

1. Clears the `DynamicBlockerHeight` + `DynamicBlockerLayerMask` overlay arrays
2. Walks every entity with `FSeinExtentsData::bBlocksFogOfWar = true`
3. For each shape, stamps the cells it covers with `(GroundHeight + BlockerHeight, LayerMask)`
4. Multiple overlapping stamps take the **taller** height + **OR'd** layer mask per cell
5. Hashes the result. If different from last tick, force-invalidates every source's stable-fast-path cache (so smoke moving onto a sightline updates LoS immediately, not on next source-pose change)

Layer-aware blockers (smoke that blocks Normal but lets Thermal through) work because the LoS test in `IsCellOpaqueToEye` checks blocker masks against the stamp's `StampBitMask`. A stamp emitting on Thermal (N1) ignores blockers whose `BlockedLayerMask` doesn't include N1.

## Static blockers (baked)

Static blockers (walls, buildings) bake into `BlockerHeight` and `BlockerLayerMask` arrays in the asset. Default bake sets all visibility bits in the layer mask (`0xFE`); a custom bake can mask per-collision-channel or per-tag if your project wants finer control.

Static + dynamic test independently in the LoS pass — a dynamic smoke grenade that doesn't block Thermal ignores a static wall's "blocks Thermal" mask. The two arrays don't get OR'd; they evaluate as separate occluders with their own masks.

## Authoring on a Blueprint

Designers add a `USeinExtentsComponent` to their actor BP — the actor-component wrapper around `FSeinExtentsData`. A `FSeinExtentsDataDetails` customization renders the layer-mask bitfield as named checkboxes resolved from plugin settings.

Same component for fog and nav. One drag-drop, two flags.

## Authoring vision sources

Likewise: `USeinVisionComponent` is the actor-component wrapper around `FSeinVisionData`. The `FSeinVisionDataDetails` customization renders stamp configs with per-stamp shape/geometry editors.

```
Actor BP — Components panel
├─ DefaultRoot (SceneComponent)
├─ Mesh (StaticMesh)
├─ SeinExtents      → FSeinExtentsData    (footprint, blocks nav, blocks fog)
├─ SeinVision       → FSeinVisionData     (stamp configs, eye height, emission mask)
├─ SeinMovement     → FSeinMovementData   (kinematics, locomotion class)
└─ SeinAbilities    → FSeinAbilityData    (granted abilities, command mappings)
```

Designers don't think about "the sim" or "components-as-payload-structs." They drag components onto the BP, set properties, hit save.

## Owned-actor visibility override

`USeinFogOfWarVisibilitySubsystem` walks every entity-actor pair each render frame and toggles `SetActorHiddenInGame` based on the local PC's vision. **One exception**: actors owned by the local player are always visible, regardless of the player's own fog state. Players see their own units even if those units happen to be standing in unexplored fog.

For "minimap shows my own units even outside my vision" UX, this is the right behavior. For RTS-classic "you can't see your own scout deep in enemy territory if it crosses unexplored fog" UX — that's not the default behavior; you'd need to gate the override.

## Vision tick interval

`USeinARTSCoreSettings::VisionTickInterval` (sim ticks). Higher = cheaper but more latency on units entering/exiting vision:

| Interval | Effective fog rate (at 30Hz sim) | Latency floor |
|---|---|---|
| 1 | 30 Hz | 33 ms |
| 3 | 10 Hz | 100 ms (RTS standard) |
| 6 | 5 Hz | 200 ms (very cheap, perceptible) |

Below ~15Hz the latency is imperceptible; above ~5Hz starts to feel stuttery. Default 3 = 10Hz fog at 30Hz sim is a sane balance.

## Render fog tick rate

Independent from `VisionTickInterval`. `USeinARTSCoreSettings::FogRenderTickRate` (Hz, float) governs only the debug-viz + UI readback cadence — not sim state. Render path runs on wall-clock and can lag freely without affecting determinism.

## Performance ballparks

Rough numbers from a 200-source scenario on a 256×256 grid:

| Action | Cost |
|---|---|
| Tick at 10Hz (most sources stable) | ~0.4 ms |
| Smoke grenade lands (forces source-cache invalidation) | ~3 ms next tick |
| 10 vehicles cross cell boundaries simultaneously | ~1 ms (10 × ~100µs per source rebuild) |

Per-tick cost is dominated by the small fraction of sources actually changing each tick. The stable-fast-path memo is what makes this cheap.
