# Blueprint Node Reference

SeinARTS exposes its functionality through **13 Blueprint Function Libraries** (BPFLs) plus ViewModel classes. Every sim-side and UI function is accessible from Blueprint without writing C++.

## Libraries by Module

### SeinARTSCoreEntity <span class="tag sim">SIM</span>

| Library | Functions | Purpose |
|---------|-----------|---------|
| [Entity](entity.md) | 8 | Entity lifecycle — spawn, destroy, transform, ownership |
| [Attributes](attributes.md) | 3 | Attribute resolution with modifier support |
| [Abilities](abilities.md) | 5 | Ability activation, cooldowns, queries |
| [Combat](entity.md#combat) | 7 | Damage, healing, spatial queries |
| [Effects](entity.md#effects) | 5 | Timed effect application and removal |
| [Player](entity.md#player) | 5 | Resource management, entity ownership |
| [Production](production.md) | 4 | Production availability, tech queries |
| [Squads](entity.md#squads) | 5 | Squad membership and queries |
| [Tags](entity.md#tags) | 5 | Gameplay tag operations |
| **Math** | **97** | Fixed-point math, vectors, quaternions, transforms, PRNG |

### SeinARTSNavigation <span class="tag sim">SIM</span>

| Library | Functions | Purpose |
|---------|-----------|---------|
| [Navigation](navigation.md) | 5 | Pathfinding requests and path queries |
| [Steering](navigation.md#steering) | 7 | Movement, rotation, flocking forces |
| [Terrain](navigation.md#terrain) | 6 | Grid queries, terrain types, walkability |

### SeinARTSUIToolkit <span class="tag render">RENDER</span>

| Library | Functions | Purpose |
|---------|-----------|---------|
| [UI Toolkit](ui-toolkit.md) | 21 | Display helpers, conversion, projection, minimap, action slots |

## Naming Convention

All Blueprint nodes follow the pattern:

```
Sein + System + Action
```

Examples:

- `SeinSpawnEntity` — Entity system, spawn action
- `SeinResolveAttribute` — Attribute system, resolve action
- `SeinWorldToMinimap` — UI system, coordinate conversion
- `SeinGetSeparationForce` — Steering system, force calculation

## Context Parameter

Most functions take a `WorldContextObject` as their first parameter. In Blueprint, this is filled automatically — you don't need to wire it. It provides access to the world subsystems.
