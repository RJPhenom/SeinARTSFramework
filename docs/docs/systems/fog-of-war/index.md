# Fog of War

<span class="tag sim">SIM</span> SeinARTS ships a pluggable fog-of-war system. The framework defines an abstract contract — `USeinFogOfWar` — and a default 2D-grid implementation, `USeinFogOfWarDefault`, with symmetric shadowcasting + a "lampshade" eye-height blocker model (CoH TrueSight behavior). Game teams can replace the implementation entirely without touching any other framework code.

## What it does

Fog-of-war owns the end-to-end vision problem for one world:

- **Bake pipeline** — capture per-cell terrain heights + static blocker geometry into a serialized asset
- **Per-player vision groups** — independent EVNNNNNN bitfield per `FSeinPlayerID` for who sees what
- **Source/blocker registration** — entities with `FSeinVisionData` register as vision sources; entities with `FSeinExtentsData::bBlocksFogOfWar` register as runtime blockers
- **Stamp updates** — recompute affected cells each sim tick (gated by `VisionTickInterval` for cost control)
- **Visibility queries** — "is this cell visible to player X?", "is this entity visible?"
- **Render-side actor toggling** — a render-tick subsystem that hides/shows actors based on the local player's stamps

Fog is fully independent of nav. The two systems share no data, no grid, no volume actor. A project can ship its own nav, its own fog, or both, and the choices are orthogonal.

## Architecture

```
ASeinFogOfWarVolume          (level actor — defines bake bounds)
    └─ TObjectPtr<USeinFogOfWarAsset>     (polymorphic baked data)

USeinFogOfWarSubsystem       (world subsystem — owns the active fog)
    └─ TObjectPtr<USeinFogOfWar>          (active impl, picked from plugin settings)

USeinFogOfWar                (abstract base — the contract)
    └─ USeinFogOfWarDefault  (shipped reference impl: 2D grid + shadowcast + lampshade)

USeinFogOfWarVisibilitySubsystem  (render-tick subsystem — toggles actor visibility from local PC's stamps)
```

The subsystem reads `USeinARTSCoreSettings::FogOfWarClass` on initialize, instantiates that class, and re-exposes it. Stamp updates run from `USeinWorldSubsystem::OnSimTickCompleted` so all clients see the same source snapshots — lockstep-safe.

## The EVNNNNNN cell bitfield

Every cell carries an 8-bit field per observer:

| Bit | Name | Purpose |
|---|---|---|
| 0 | E | Explored — sticky once set, indicates "ever been visible" (drives the dimmed minimap "memory" look) |
| 1 | V | Visible-Normal — current generic visibility |
| 2 | N0 | Custom layer 0 (designer-named) |
| 3 | N1 | Custom layer 1 |
| 4 | N2 | Custom layer 2 |
| 5 | N3 | Custom layer 3 |
| 6 | N4 | Custom layer 4 |
| 7 | N5 | Custom layer 5 |

Bits 2-7 are designer-configurable layers — name them "Stealth", "Thermal", "Radar", "Acoustic", whatever your project needs. See [Vision Layers](vision-layers.md).

## When to use what

| Need | API |
|---|---|
| Is a cell visible to player X right now? | `USeinFogOfWarBPFL::SeinIsCellVisible(WorldPos, LayerName)` |
| Has player X ever seen this cell? | `USeinFogOfWarBPFL::SeinIsCellExplored` |
| Is this entity visible to player X? | `USeinFogOfWarBPFL::SeinIsEntityVisible` |
| Make an entity emit vision | Add `FSeinVisionData` with stamp configs |
| Make an entity emit fog occlusion at runtime | `FSeinExtentsData` with `bBlocksFogOfWar = true` |
| Toggle actor render visibility from local player's view | Automatic — `USeinFogOfWarVisibilitySubsystem` polls each render frame |

All sim queries are `FFixedVector` — no float on the determinism path. Render-side toggling reads from the deterministic stamp state but ticks on wall-clock for high-rate UX response.

## Plugin settings

Configured in **Project Settings → Plugins → SeinARTS → Fog Of War**:

- **Fog Of War Class** — class picker filtered to `USeinFogOfWar` subclasses. Empty defaults to `USeinFogOfWarDefault`.
- **Vision Cell Size** — fallback grid cell edge in world units. Per-volume override on `ASeinFogOfWarVolume`. Independent of nav cell size — fog is typically coarser.
- **Vision Layers** — 6 designer-configurable custom layer slots (bits 2..7). The framework "Normal" layer is bit 1, always present. See [Vision Layers](vision-layers.md).
- **Vision Tick Interval** — recompute cadence in sim-ticks (e.g. 3 at 30Hz sim = 10Hz fog). Higher = cheaper but more latency.
- **Fog Render Tick Rate** — render-side debug viz cadence (independent of sim).

## Pages in this section

- [Default Implementation](default-impl.md) — what ships, the shadowcast algorithm, lampshade eye-height model
- [Bake Pipeline](bake-pipeline.md) — what the bake captures, how to invalidate, multi-volume layout
- [Vision Layers](vision-layers.md) — Normal vs custom layers, perception/emission masks, stealth pattern
- [Sources & Blockers](sources-blockers.md) — `FSeinVisionData` source authoring, dynamic blockers (smoke, etc.)
- [Custom Implementations](custom.md) — replacing `USeinFogOfWar` with your own (raymarched volumetric, GPU-driven, simpler grid, etc.)

## Console commands

- `Sein.FogOfWar.Show [0|1|on|off]` — toggle fog debug viz across all viewports (custom show flag — no UE built-in equivalent)
- `Sein.FogOfWar.Show.Player <id>` — render a specific player's vision instead of the local PC's (lockstep symmetric — every client has every player's group)
- `Sein.FogOfWar.Show.Layer <bit 0..7>` — paint cells through a specific layer bit (0=E, 1=V, 2..7=N0..N5). No args resets to V.

## Module layout

| Module | Owns |
|---|---|
| `SeinARTSFogOfWar` | `USeinFogOfWar` abstract base, `USeinFogOfWarDefault` impl, `ASeinFogOfWarVolume`, `USeinFogOfWarSubsystem`, `USeinFogOfWarVisibilitySubsystem`, BPFL, debug viz scene-proxy, volume details panel |
| `SeinARTSCoreEntity` | `FSeinVisionData` (sim component) — fog reads it via reflection-keyed storage; sim doesn't reference the fog module |

The fog module never reaches into sim internals beyond reading `FSeinVisionData` and `FSeinExtentsData` through the generic component storage. The sim never includes fog headers — it talks to fog through one cross-module delegate (`USeinWorldSubsystem::LineOfSightResolver`) for ability LoS checks.
