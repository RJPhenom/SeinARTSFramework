# Default A* Implementation

<span class="tag sim">SIM</span> `USeinNavAStar` is the shipped reference implementation of `USeinNavigation`. It's a single-layer 2D grid with synchronous A* pathfinding and line-of-sight path smoothing. Minimal on purpose — small enough to read end-to-end, deterministic, suitable for small-to-medium RTS maps.

## Design

- **Grid:** flat 2D, row-major, indexed by `(X, Y)` cell coordinates. Per-cell: `Cost` (1 = walkable, 0 = blocked), `Connections` (8-direction bitmask), `Height` (world Z at cell center).
- **A* search:** 8-direction (cardinal + diagonal), connectivity gated by the per-cell `Connections` bitmask (baked, not live-computed). Octile distance heuristic.
- **Path smoothing:** Bresenham line-of-sight pass after A* — flattens stair-stepped grid paths into straight runs that follow the cell graph's actual walkability.
- **Storage:** all runtime arrays are flat `TArray<uint8>` / `TArray<FFixedPoint>`. Bake output serializes to `USeinNavAStarAsset` for level-load reuse.

## When to use it

A* + 2D grid is a good fit when:

- Maps fit in memory at your chosen cell size (~100k–1M cells is comfortable; 10M starts to hurt)
- Vertical traversal is mostly "walk on top" rather than complex multi-layer pathing (caves, bridges, floor-over-floor)
- Unit count per level is RTS-typical (low hundreds, not thousands of pathing agents)
- Cell-level fidelity is enough — no need for narrow gaps that don't align to the grid

If you need any of: hierarchical pathing for huge maps, true 3D nav, navmesh-like cell-shape adaptation, dynamic obstacle insertion at sub-cell granularity — replace it. See [Custom Implementations](custom.md).

## Class properties

`USeinNavAStar` (configurable on the CDO via class defaults — class is instantiated from plugin settings, so values apply project-wide):

| Property | Default | Purpose |
|---|---|---|
| `MaxWalkableSlopeDegrees` | 45° | Surfaces steeper than this bake as blocked |
| `BakeTraceHeadroom` | 200 | Vertical extent above tallest volume that bake traces start from |
| `bDrawCellsInDebug` | true | Emit cell quads (green = walkable, red = blocked) for scene-proxy viz |

These edit on the nav class CDO. To customize per-project, subclass `USeinNavAStar` and pick your subclass in plugin settings — or just edit the CDO directly via Project Settings → Plugins → SeinARTS → Navigation Class CDO if you ship the default.

## How `FindPath` works

```
FindPath(Request, OutPath):
    1. World→grid: snap Request.Start and Request.End to cell coords
    2. Build the dynamic-blocked overlay for THIS request:
       a. Walk DynamicBlockers, skipping the requester (self-exclusion)
       b. Skip blockers whose BlockedNavLayerMask doesn't intersect Request.AgentNavLayerMask
       c. Stamp remaining blockers' shapes into the overlay grid
    3. A* search the start cell to the end cell, gated by static cells AND the overlay
    4. Smooth the cell-chain into a world-space waypoint list (Bresenham LoS pass)
    5. Return FSeinPath with bIsValid = true
```

Self-exclusion + per-agent layer filtering both happen at request time. This means an amphibious unit pathing through water cells (which a "Default-only" water blocker treats as walkable) costs nothing at bake — it's a pure runtime mask check.

The full search is synchronous on the calling thread. For RTS-scale unit counts at a 100 cm cell size, A* searches typically resolve in well under a millisecond. If your project pushes harder (massive maps + lots of concurrent path requests), profile before reaching for an async path strategy.

## Cell connectivity (baked)

The 8-direction `Connections` bitmask is computed at bake time, not at query time. Each bit `N` is set iff a unit can traverse from this cell to its neighbor in direction `N` (0..3 cardinal, 4..7 diagonal).

The bake checks both halves of each edge with a midpoint trace + slope check + max-step-height clamp. This means A* is a pure cell-graph walk at runtime — no slope math, no step-height comparisons, just "is bit N set?" The rules applied at bake are guaranteed to match the rules enforced at runtime, by construction.

## Asset format

`USeinNavAStarAsset` (the bake output):

| Field | Type | Purpose |
|---|---|---|
| `Width`, `Height` | `int32` | Grid dimensions in cells |
| `CellSize` | `FFixedPoint` | World units per cell edge |
| `Origin` | `FFixedVector` | World-space XY of cell `(0,0)`'s bottom-left corner |
| `Cells` | `TArray<FSeinAStarCell>` | Flat row-major cells: `Cost`, `Connections`, `Height` |

Stored as a `UDataAsset` under `/Game/NavigationData/` after bake. The volume's `BakedAsset` field is auto-assigned to the saved asset.

## Determinism notes

- All sim-side queries are `FFixedPoint`-only. No float comparisons in `FindPath`, `IsPassable`, `IsReachable`.
- Bake pipeline uses float traces (UE collision is float-native) but quantizes results to `FFixedPoint` before serialization. Two clients re-baking the same level on different hardware get identical asset bytes when their float trace results agree to the quantum (typical case on stock x86_64 builds).
- Bake is editor-only. Shipped builds load the asset and never re-trace.

## Performance ballparks

These are rough numbers from a test scenario; profile your own.

| Scenario | Time |
|---|---|
| 100 cm cell grid, 256×256 cells, single A* search | ~0.1 ms |
| Same, with 100 dynamic blockers stamped per FindPath | ~0.3 ms |
| Bake of a 1000×1000 cell volume on a fast desktop | ~5 s (synchronous, blocks editor) |

The bake is the slow path. Per-frame query cost is negligible at RTS scales. If you push to 10000×10000 the bake hits multiple minutes — at that size you want a different impl anyway.

## Limitations

`USeinNavAStar` is intentionally minimal:

- Single-layer only. No multi-floor pathing, no bridges that cross over walkable surfaces beneath them.
- No hierarchical pathing. Long-distance paths on huge maps cost full A* every time.
- Synchronous. A FindPath happens on the requesting thread; a stall in the search stalls the sim tick.
- No partial-path support. If the destination is unreachable, you get `bIsValid = false`. Roll your own approximation (snap-to-nearest-reachable, give up gracefully) at the call site.

For projects that need any of those: see [Custom Implementations](custom.md). For projects that don't: this impl is fine, and rebuilding it isn't worth your time.
