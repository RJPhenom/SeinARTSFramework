# Fog of War Bake Pipeline

<span class="tag editor">EDITOR</span> The fog-of-war bake captures per-cell terrain heights and static sight blocker geometry into a serialized asset. Independent of the nav bake — fog has its own volume actor (`ASeinFogOfWarVolume`) and asset class.

## The bake button

Drop an `ASeinFogOfWarVolume` into the level (Place Actors → SeinARTS → Sein Fog Of War Volume). Scale to cover your visibility region. Open its details panel — there's a "Bake Fog Of War" button at the top:

```
┌─ SeinARTS Fog Of War Build ─────────────────┐
│ Bake the fog-of-war grid covering every     │
│ Sein Fog Of War Volume in this level.       │
│ Per-cell downward traces capture terrain    │
│ height + detect static sight blockers       │
│ (walls, buildings). Output is serialized to │
│ /Game/FogOfWarData/ and auto-assigned to    │
│ every volume's Baked Asset.                 │
│                                             │
│      [    Bake Fog Of War    ]              │
└─────────────────────────────────────────────┘
```

The button is provided by `FSeinFogOfWarVolumeDetails`, which lives in `SeinARTSFogOfWar/Private/Editor/`. It calls `USeinFogOfWarSubsystem::BeginBake(World)`, which dispatches to the active `USeinFogOfWar` subclass. Same impl-agnostic pattern as the nav bake.

## What gets baked (default impl)

For every cell inside the union of all volume bounds, `USeinFogOfWarDefault::DoSyncBake`:

1. Traces downward from the volume top + headroom, sampling terrain Z (the **ground trace**)
2. Traces downward again from above, but stopping at the first hit higher than terrain (the **blocker trace**)
3. If `(blocker hit Z - ground hit Z) >= StaticBlockerMinHeight`, registers a static blocker
4. Quantizes ground and blocker heights to 8-bit values: `world_z = MinHeight + Encoded * HeightQuantum`
5. Sets `BlockerLayerMask = 0xFE` (every visibility bit) for the MVP — per-layer blocker masks (e.g. smoke that blocks Normal but not Thermal) is a designer-authored runtime concern, not a bake concern
6. Serializes to `USeinFogOfWarDefaultAsset` under `/Game/FogOfWarData/<level-name>.uasset`
7. Writes the asset reference into every volume's `BakedAsset` field

## Per-volume bake-static-blockers toggle

`ASeinFogOfWarVolume::bBakeStaticBlockers` (default true) controls whether the bake's per-cell blocker trace runs. When false, the bake captures grid layout + ground height only, and `BlockerHeight` stays zero everywhere. All sight occlusion then comes from runtime sources:

- `FSeinExtentsData` with `bBlocksFogOfWar = true`
- Designer-authored ability effects that drop blockers

This is the right setting for projects with mostly-flat outdoor terrain where buildings are sparse and dynamic. With static blockers off, the bake is much cheaper.

When multiple volumes exist on a level, the **first volume's setting wins** — same convention as `CellSize`. Mixed bake-static-blockers across volumes isn't supported in the default impl (a custom impl could).

## Multi-volume layout

Same as nav: multiple `ASeinFogOfWarVolume`s union into one baked asset. Use multiple volumes when:

- Map has separated visibility regions
- You want different `CellSize` per region (typically fog is coarser than nav — 200-400cm cells are common, vs. 50-100cm for nav)

Per-volume override on `ASeinFogOfWarVolume`:

- `bOverrideCellSize` + `CellSize` — falls back to plugin settings' `VisionCellSize`

## Editor-baked bounds snapshot

Identical pattern to `ASeinNavVolume`. `PostEditMove` writes `PlacedBoundsMin/Max` (`FFixedVector`) snapshots so runtime never converts the float `FBox` from the brush. Avoids cross-arch float drift at level load.

## Loading at runtime

`USeinFogOfWarSubsystem::OnWorldBeginPlay`:

1. Walks every `ASeinFogOfWarVolume`, hands the first non-null `BakedAsset` to `USeinFogOfWar::LoadFromAsset`
2. If no baked asset, calls `USeinFogOfWar::InitGridFromVolumes(World)` so the impl can auto-size a grid for stamping + debug viz to work pre-bake

The default impl's `InitGridFromVolumes` walks volume bounds and builds a runtime grid by per-cell terrain trace, capped at `InitTraceCellCap` cells (default 20000) to avoid editor stalls on huge unbaked maps. Cells beyond the cap snap to volume mid-Z.

If neither asset nor auto-init produces grid dims, queries return no-visibility:

- `HasRuntimeData()` returns `false`
- `GetCellBitfield` returns 0
- `IsCellVisible` returns false (with one exception: the `LineOfSightResolver` permissive fallback — see below)

## Permissive fallback for LOS resolver

The cross-module `USeinWorldSubsystem::LineOfSightResolver` delegate (bound by `USeinFogOfWarSubsystem` at world begin-play) returns `true` when the fog impl has no runtime data:

```cpp
[FogWeak](FSeinPlayerID Observer, const FFixedVector& TargetWorld) -> bool
{
    USeinFogOfWar* Fog = FogWeak.Get();
    if (!Fog || !Fog->HasRuntimeData()) return true;  // permit when no data
    return Fog->IsCellVisible(Observer, TargetWorld, SEIN_FOW_BIT_NORMAL);
}
```

This makes tests and fog-less games "just work" — abilities that require LoS pass when no fog data exists. The BPFL reader functions follow the same convention.

## Re-baking

Click the button again. Walks current geometry from scratch. The default impl has no incremental bake — a custom impl could.

## Bake invalidation triggers

You should re-bake when:

- Static sight-blocking geometry moves (walls, buildings, hedgerows)
- A volume is moved, scaled, deleted
- Plugin-settings vision layers are reordered (rare; only append, never reorder, per the layer determinism contract)

You don't need to re-bake when:

- Vision sources move (handled by per-tick stamping)
- A unit's `FSeinVisionData` config changes at runtime
- A dynamic blocker (smoke, destructibles) appears

## Programmatic bake

```cpp
// Editor automation
if (UWorld* World = GEditor->GetEditorWorldContext().World())
{
    USeinFogOfWarSubsystem::BeginBake(World);
    // Sync in default impl — call returns when done.
    // Custom impls may be async; poll IsBaking(World) to wait.
}
```

## CustomizeVolumeDetails hook

Subclasses of `USeinFogOfWar` can extend the volume's details panel by overriding `CustomizeVolumeDetails(IDetailLayoutBuilder&, ASeinFogOfWarVolume*)`. The framework's `FSeinFogOfWarVolumeDetails` calls this after adding its own bake button row. Same pattern as nav.

Useful for:

- Per-volume layer enable/disable toggles
- Per-bake quantization options (e.g. 16-bit heights for tall maps)
- Diagnostics readout (cell count, bake size, last bake timestamp)
