# Navigation

<span class="tag sim">SIM</span> SeinARTS ships a pluggable navigation system. The framework defines an abstract contract — `USeinNavigation` — and a default A* grid implementation, `USeinNavAStar`. Game teams can replace the implementation entirely without touching any other framework code.

## What it does

Navigation owns the end-to-end pathing problem for one world:

- **Bake pipeline** — convert level geometry inside `ASeinNavVolume`s into a serialized data asset
- **Runtime queries** — `FindPath`, `IsPassable`, `IsReachable`, `GetGroundHeightAt`, `ProjectPointToNav`
- **Dynamic blockers** — re-stamp moving entities (vehicles, buildings under construction) into the grid each sim tick
- **Debug viz** — emit cell-quad geometry for the scene-proxy debug overlay

The simulation never reaches into nav internals. It talks to nav through:

1. The `USeinMoveToAction` consuming `USeinNavigation::FindPath`
2. The `USeinWorldSubsystem::PathableTargetResolver` delegate (bound by `USeinNavigationSubsystem` at world begin-play) for ability validation

A team replacing the nav class never needs to touch `SeinARTSCoreEntity`, `SeinARTSMovement`, or `SeinARTSFramework` — the abstract surface is the boundary.

## Architecture

```
ASeinNavVolume               (level actor — defines bake bounds)
    └─ TObjectPtr<USeinNavigationAsset>    (polymorphic baked data)

USeinNavigationSubsystem     (world subsystem — owns the active nav)
    └─ TObjectPtr<USeinNavigation>         (active impl, picked from plugin settings)

USeinNavigation              (abstract base — the contract)
    └─ USeinNavAStar         (shipped reference impl: 2D grid + A* + LoS smoothing)
```

The subsystem reads `USeinARTSCoreSettings::NavigationClass` on initialize, instantiates that class, and re-exposes it. Clients route through `USeinNavigationSubsystem::GetNavigationForWorld(WorldContextObject)`.

## When to use what

| Need | API |
|---|---|
| Path from A to B for a unit | `USeinMoveToProxy::SeinMoveTo` (Blueprint async) or `USeinNavigation::FindPath` directly |
| Check if a target is reachable (ability validation) | `USeinNavigation::IsReachable` — auto-routed via the cross-module delegate |
| Snap a point to walkable ground | `USeinNavigation::ProjectPointToNav` |
| Sample ground Z at an XY | `USeinNavigation::GetGroundHeightAt` |
| Check static walkability of a cell | `USeinNavigation::IsPassable` |
| Mark an entity as a path blocker | Add `FSeinExtentsData` with `bBlocksNav = true` |

All queries take and return `FFixedPoint` / `FFixedVector` — there's no float on the sim path. Float conversions happen only at the `FVector` boundary in debug rendering.

## Plugin settings

Configured in **Project Settings → Plugins → SeinARTS → Navigation**:

- **Navigation Class** — class picker filtered to `USeinNavigation` subclasses. Empty defaults to `USeinNavAStar`.
- **Default Cell Size** — fallback cell edge in world units. Per-volume override on `ASeinNavVolume`.
- **Default Max Step Height** — fallback vertical traversal step in world units. Per-volume override.
- **Nav Layers** — 7 designer-configurable agent/blocker layer slots (bit 1..7). The reserved "Default" layer is bit 0 and always present. See [Layers & Blockers](layers-blockers.md).

## Pages in this section

- [Default A* Implementation](default-astar.md) — what ships, how the grid is structured, A* + LoS smoothing
- [Bake Pipeline](bake-pipeline.md) — the bake button, what gets baked, how to invalidate
- [Layers & Blockers](layers-blockers.md) — agent layer masks, dynamic blocker stamping, the amphibious-vehicle pattern
- [Custom Implementations](custom.md) — replacing `USeinNavigation` with your own (Recast, navmesh, hierarchical graph, etc.)

## Console commands

- `Sein.Nav.Show [0|1|on|off]` — toggle the nav debug viz across all viewports (same bit as UE's `P` hotkey + `showflag.navigation`)
- `Sein.Nav.Show.Layer <bit 0..7>` — filter blocker rendering to a specific agent layer (no args = show all)
- `Sein.Nav.Show.SteeringVectors [0|1|on|off]` — toggle per-unit steering bias arrows (provided by `SeinARTSMovement`)

## Module layout

| Module | Owns |
|---|---|
| `SeinARTSNavigation` | `USeinNavigation` abstract base, `USeinNavAStar` default impl, `ASeinNavVolume`, `USeinNavigationSubsystem`, `USeinNavigationBPFL`, debug viz scene-proxy, volume details panel |
| `SeinARTSMovement` | `USeinMovement` abstract steering base + 4 shipped subclasses, `USeinMoveToAction`, `USeinMoveToProxy`, active-move debug overlay |

The two modules are split intentionally: a team replacing the nav can keep using the shipped movement classes since `USeinMovement` consumes nav only through the abstract `USeinNavigation*` (specifically `IsPassable` + `GetGroundHeightAt`).
