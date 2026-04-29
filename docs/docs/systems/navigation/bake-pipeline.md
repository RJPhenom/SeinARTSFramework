# Navigation Bake Pipeline

<span class="tag editor">EDITOR</span> The bake pipeline converts level geometry inside one or more `ASeinNavVolume` actors into a serialized data asset that the runtime nav loads at level begin-play. Designers click one button.

## The bake button

Drop an `ASeinNavVolume` into the level (Place Actors → SeinARTS → Sein Nav Volume). Scale it to cover your walkable area. Open its details panel — there's a "Bake Navigation" button at the top:

```
┌─ SeinARTS Nav Build ────────────────────────┐
│ Build the nav grid covering every Sein Nav  │
│ Volume in this level.                       │
│ Bake runs in the background — cancel or     │
│ watch progress in the toast.                │
│                                             │
│        [    Bake Navigation    ]            │
└─────────────────────────────────────────────┘
```

The button is provided by `FSeinNavVolumeDetails`, which lives in `SeinARTSNavigation/Private/Editor/`. It calls `USeinNavigationSubsystem::BeginBake(World)`, which dispatches to whatever `USeinNavigation` subclass is active. **The button is impl-agnostic** — a custom nav class with a different bake strategy still gets the same designer button.

## Multi-volume layout

Multiple volumes union into one baked asset. Use multiple volumes when:

- Your map has multiple walkable regions separated by void (islands, multi-room interiors)
- You want different `CellSize` per region — cover one volume at fine grain, the next at coarse
- You want different `MaxStepHeight` per region — fine-grained terrain in one zone, blocky platform geometry in another

Per-volume overrides on `ASeinNavVolume`:

- `bOverrideCellSize` + `CellSize` — falls back to plugin settings' `DefaultCellSize`
- `bOverrideMaxStepHeight` + `MaxStepHeight` — falls back to plugin settings' `DefaultMaxStepHeight`

## What gets baked (default A* impl)

For every cell inside the union of all volume bounds, `USeinNavAStar::DoSyncBake`:

1. Traces downward from the volume top + headroom, sampling terrain Z and surface normal
2. Marks cell `Cost = 0` (blocked) if:
   - No surface hit (cell is over void)
   - Surface slope > `MaxWalkableSlopeDegrees`
   - Cell occupies a static collision blocker (walls, buildings)
3. For each walkable cell, computes the 8-direction `Connections` bitmask via per-edge midpoint traces + slope + step-height checks
4. Stores `Cost`, `Connections`, and surface `Height` in the cell array
5. Quantizes float trace results to `FFixedPoint` (deterministic across clients with stock IEEE-754)
6. Serializes the result to `USeinNavAStarAsset` under `/Game/NavigationData/<level-name>.uasset`
7. Writes the asset reference into every volume's `BakedAsset` field

The bake is synchronous and editor-blocking. A `FScopedSlowTask` shows progress and supports cancel. Cancelling discards in-progress work and leaves the previous asset intact.

## Editor-baked bounds snapshot

`ASeinNavVolume` and `ASeinFogOfWarVolume` both write `PlacedBoundsMin/Max` (`FFixedVector`) snapshots in `PostEditMove`. Reasoning: `AVolume`'s native bounds are `FBox` (float). At runtime, converting that float `FBox` into `FFixedVector` would happen on every client and produce subtly different results across CPU architectures with different float behavior. The editor snapshot pre-converts once, serializes to the `.umap` as int64 bits, and runtime reads those directly — no `FromFloat` ever runs at level load.

`bBoundsBaked` flips true on first snapshot. Volumes from before this field existed read it as false; their float bounds get used as-is at runtime (no determinism concern in single-arch testing, just less robust).

## Loading at runtime

`USeinNavigationSubsystem::OnWorldBeginPlay` walks every `ASeinNavVolume` in the world and hands the first non-null `BakedAsset` to `USeinNavigation::LoadFromAsset`. **Last-baked wins** if multiple volumes hold different assets — the framework picks the first one found. In practice, a single bake produces one asset shared by all volumes, so this only matters if you hand-edit volume assets.

If no volume has a baked asset, the runtime nav stays empty:

- `HasRuntimeData()` returns `false`
- `FindPath` returns `bIsValid = false`
- `IsPassable` returns `false`
- The cross-module `PathableTargetResolver` delegate falls back to "permit" (so tests + nav-less games still work)

## Re-baking

Click the button again. The bake walks current level geometry from scratch — no incremental support in the default impl. The previous asset gets overwritten in place; references stay valid.

A custom nav impl can implement incremental bakes (re-bake only the cells inside dirty volumes, only the cells under modified geometry, etc.). The framework's bake button calls into your `BeginBake(World)` virtual, so the strategy is yours.

## Bake invalidation triggers

You should re-bake when:

- Level static geometry moves (walls, terrain, building meshes)
- A volume is moved, scaled, or deleted
- `MaxWalkableSlopeDegrees`, `MaxStepHeight`, or `CellSize` changes
- Plugin-settings nav layers are reordered (rare; only append to NavLayers, never reorder, per the layer determinism contract)

You don't need to re-bake when:

- Dynamic entities (units, vehicles) move
- A unit's `FSeinExtentsData::bBlocksNav` flips at runtime
- A new dynamic blocker entity spawns

Those go through the per-tick `FSeinNavBlockerStampSystem` (PreTick, priority 7) and don't touch the static asset.

## Programmatic bake

For automation / CI:

```cpp
// In editor code — fire a bake from a python tool, a custom toolbar button, etc.
if (UWorld* World = GEditor->GetEditorWorldContext().World())
{
    USeinNavigationSubsystem::BeginBake(World);
    // Returns true if the bake started; false if already in progress or no volumes.
    // Poll IsBaking(World) to wait.
}
```

The default impl's bake is synchronous so the call returns when the bake is done. Custom impls may be async — check `USeinNavigationSubsystem::IsBaking(World)` to wait.

## CustomizeVolumeDetails hook

Subclasses of `USeinNavigation` can extend the volume's details panel with their own rows by overriding `CustomizeVolumeDetails(IDetailLayoutBuilder&, ASeinNavVolume*)`. The framework's `FSeinNavVolumeDetails` calls this after adding its own bake button row, so a custom impl can layer on:

- Per-bake options (export-to-disk vs. in-memory, multi-stage bake stages)
- Per-volume diagnostics (cell counts, bake timestamp, asset disk size)
- Custom buttons (validate, export to text, profile)

Default impl is a no-op; subclasses without an override see no change.
