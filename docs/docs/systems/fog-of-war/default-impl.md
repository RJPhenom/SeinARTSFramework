# Default Fog of War Implementation

<span class="tag sim">SIM</span> `USeinFogOfWarDefault` is the shipped reference implementation. It's a 2D grid with symmetric shadowcasting visibility + a "lampshade" eye-height blocker model — the classic CoH TrueSight behavior, where a unit on a roof can see down into a courtyard but a unit at street level can't see through the wall.

## Design

- **Grid:** flat 2D, row-major. Per-cell: terrain height (`GroundHeight`), static blocker height (`BlockerHeight`), static blocker layer mask (`BlockerLayerMask`), runtime dynamic blocker height (`DynamicBlockerHeight`), dynamic blocker layer mask (`DynamicBlockerLayerMask`).
- **Per-observer state:** one `FSeinFogVisionGroup` per `FSeinPlayerID`, lazily created on first stamp. Each group holds the EVNNNNNN bitfield + per-bit refcount arrays.
- **Stamp model:** delta-refcount. A source whose pose AND stamp set hash to the last-tick value skips entirely (no work). On change: walk stored footprints, decrement old refcounts, re-stamp every shape, accumulate footprints into new arrays.
- **Visibility test:** for each candidate cell within a stamp's geometry, run integer-Bresenham line-of-sight from the source's eye position to the cell. The ray's Z linearly interpolates between eye Z and target cell Z; cells whose blocker height exceeds the ray Z occlude.

## When to use it

The default impl is a good fit when:

- 2D grid fog matches your camera perspective (top-down, mid-angle isometric, RTS standard)
- You want CoH-style TrueSight (height-aware, "see-from-the-roof" behavior)
- Per-player fog state with up to 8 simultaneous players is fine
- 6 custom layer bits is enough for your perception channels

If you need volumetric fog (fully 3D), GPU-driven raymarch fog, simpler "everyone sees everything" mode, or radically different perception semantics — replace it. See [Custom Implementations](custom.md).

## Determinism contract

The shipped impl honors the sim's full determinism contract:

- **Stamp output (CellBitfield):** fully `FFixedPoint`. `StampFlatCircle` uses `FFixedPoint` radius math + cell² integer comparisons. No float on the stamp path.
- **Stamp cadence:** driven by `USeinWorldSubsystem::OnSimTickCompleted` gated by plugin-settings `VisionTickInterval`. All clients stamp against the same tick-N source snapshot — lockstep-safe.
- **LoS query:** `FSeinLineOfSightResolver` takes `FFixedVector` end-to-end. No float round-trip between sim caller and impl cell lookup.
- **Grid layout:** derived from volume bounds via the editor-baked `PlacedBoundsMin/Max` snapshots (see [Bake Pipeline](bake-pipeline.md)). Deterministic on IEEE-754 targets with stock UE FP modes.
- **Bake:** editor-only. Produces quantized per-cell ground + blocker heights stored in `USeinFogOfWarDefaultAsset`. Runtime dequantizes into `FFixedPoint` arrays on load.
- **Debug viz:** float conversions at the `FFixedPoint → FVector` boundary, render-only, not sim state.

## Class properties

Configurable on the CDO via class defaults:

| Property | Default | Purpose |
|---|---|---|
| `BakeTraceHeadroom` | 400 | Vertical extent above tallest fog volume that bake traces start from |
| `StaticBlockerMinHeight` | 15 | Min gap between top/ground trace hits to register as a blocker (filters mesh-thickness noise) |
| `BakeTraceChannel` | `ECC_Visibility` | Collision channel used for ground + blocker traces |
| `InitTraceCellCap` | 20000 | Cell-count cap for the auto-init terrain trace pass (shipping-build-time fallback when no bake is loaded; bake itself has no cap) |

## How TickStamps works

```
TickStamps(World):
    1. Rebuild dynamic blocker overlay from FSeinExtentsData entities (bBlocksFogOfWar set)
       - Hash compares against last tick — if changed, invalidates every source's
         stable-fast-path cache so smoke moving onto a sightline updates LoS
    2. Walk every entity carrying FSeinVisionData
    3. For each source, call UpdateSourceStamp:
       a. Compare new pose (WorldPos + Rotation) + stamp hash against memoized state
       b. Identical inputs → skip (the perf win — most units don't cross cells per tick)
       c. Changed inputs → decrement old footprint refcounts, re-stamp every shape
    4. Sources that vanished (entity destroyed) get their footprints torn down
```

The "stable-fast-path" cache is what makes per-tick stamping cheap. A 200-unit RTS scenario typically has fewer than 20 source updates per tick (units crossing cells, abilities firing, buildings completing). The other 180+ short-circuit on the pose hash compare.

## How shadowcasting works

For each shape on the source (radial, rect, cone), the stamp walks candidate cells defined by the geometry. Per-cell:

1. Compute eye world position: `EntityWorldPos + Quat(EntityYaw)·LocalOffset`. So a building's window cone casts from the window, not the building center.
2. Eye Z = `EntityWorldPos.Z + EyeHeight` — uses the unit's actual sim Z, not the cell's baked ground (a unit on a roof reads its standing surface, not the ground beneath).
3. Run integer-Bresenham line-of-sight from `(EyeX, EyeY, EyeZ)` to `(CellX, CellY, CellZ)` where cell Z is the cell's baked ground height.
4. At each intermediate cell along the ray, the ray's Z interpolates linearly. Cell occludes if:
   - `GroundHeight > RayZ` (terrain always occludes), OR
   - `BlockerHeight > RayZ AND (BlockerLayerMask & StampBitMask) != 0`, OR
   - `DynamicBlockerHeight > RayZ AND (DynamicBlockerLayerMask & StampBitMask) != 0`
5. If LoS is unbroken, the candidate cell gets the source's bit stamped (V for Normal, N0..N5 for custom layers).

Per-target LoS is `O(R)` per cell where R is the ray length in cells (worst case O(R³) for a stamp covering R³ cells, but the bound is loose since LoS work scales with the ray, not the area).

## Lampshade behavior

The "lampshade" name comes from the eye-height occlusion shape: a unit's vision is bounded above by their `EyeHeight` cone. A 180cm-tall infantryman behind a 200cm wall sees nothing on the other side. A unit on a roof has its eye Z elevated by the roof's height, so the ray descends as it crosses the wall — the wall, taller than the floor's eye Z but shorter than the roof's, only occludes from the floor.

This is what makes CoH-style TrueSight feel right. Walls block infantry, but units on elevated terrain (or in high windows) get genuine elevation advantage.

## Asset format

`USeinFogOfWarDefaultAsset`:

| Field | Type | Purpose |
|---|---|---|
| `Width`, `Height` | `int32` | Grid dims in cells |
| `CellSize` | `FFixedPoint` | World units per cell edge |
| `Origin` | `FFixedVector` | World-space XY of cell `(0,0)`'s bottom-left |
| `MinHeight`, `HeightQuantum` | `FFixedPoint` | Quantization constants for height bytes |
| `Cells[]` | `TArray<FSeinFogOfWarCell>` | Per-cell `GroundHeight` (uint8), `BlockerHeight` (uint8), `BlockerLayerMask` (uint8) |

Heights are 8-bit quantized: `world_z = MinHeight + GroundHeight * HeightQuantum`. 256 steps over the map's vertical range. With a 10cm quantum that's 25.6m of dynamic range — comfortable for most RTS levels.

## Per-player vision groups

`FSeinFogVisionGroup` (one per `FSeinPlayerID`):

```cpp
struct FSeinFogVisionGroup
{
    TArray<uint8> CellBitfield;   // EVNNNNNN per cell
    TArray<uint16> RefCounts[8];  // Per-bit refcount, [1..7] (bit 0 = Explored, sticky, no refcount)
};
```

Bit `b` is set iff `RefCounts[b]` > 0 for that cell. When a refcount transitions 0→1 the bit lights up; 1→0 the bit clears. Explored is sticky once set, no refcount.

Lazy creation: a player's group is allocated on their first source stamp. Neutral (player ID 0) is a legal key — useful for neutral-owned structures with passive vision (radar towers, observation posts).

`uint16` caps simultaneous overlap at 65535 sources/cell. Well above any realistic RTS scenario.

## Performance ballparks

Rough numbers from a test scenario with 200 vision sources on a 256×256 grid:

| Metric | Value |
|---|---|
| Cold tick (full re-stamp) | ~3 ms |
| Steady state (most sources stable) | ~0.4 ms |
| Per-source LoS work | ~5 µs (typical) |

Vision tick at 10Hz (interval 3 at 30Hz sim) keeps fog cost well under 5% of a tick. Bake of a 1000×1000 cell volume runs in ~10s on a fast desktop.

## Limitations

The default impl is intentionally minimal:

- 2D grid only. No volumetric fog.
- 256-quantized heights. Maps with > 25m vertical range need a larger `HeightQuantum`.
- Synchronous LoS rays. No GPU-driven version.
- All work happens on the sim thread. No worker pool.

For projects that need any of those: see [Custom Implementations](custom.md).
