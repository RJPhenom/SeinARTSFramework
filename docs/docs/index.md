---
hide:
  - navigation
  - toc
---

<div class="sein-hero" markdown>

# SeinARTS Framework

Deterministic lockstep RTS framework for Unreal Engine 5.
Build Company of Heroes-scale games with Blueprint-first design.

[Get Started](getting-started/index.md){ .md-button .md-button--primary }
[View on GitHub](https://github.com/YOUR_USERNAME/SeinARTSFramework){ .md-button }

</div>

<div class="feature-grid" markdown>

<div class="feature-card" markdown>
### Deterministic Simulation
All game logic runs on fixed-point math (32.32 format) with a deterministic PRNG. Guaranteed identical results across platforms — the foundation of lockstep networking.
</div>

<div class="feature-card" markdown>
### Blueprint-First Design
Abilities, units, damage formulas, AI behaviors, and UI are all composed in Blueprint. C++ provides the deterministic primitives. Designers build the game.
</div>

<div class="feature-card" markdown>
### Everything is an Ability
Movement, attack, harvest, build, patrol, garrison — every command is an ability Blueprint with cooperative latent execution. No hardcoded command enums, ever.
</div>

<div class="feature-card" markdown>
### Blueprint IS the Unit
Each unit type is a Blueprint subclass. Archetype data, sim components, and visual config all live on the Blueprint CDO. One asset, single source of truth.
</div>

<div class="feature-card" markdown>
### Sim/Render Separation
The simulation never touches floats, AActors, or UE subsystems. The visual layer reads from the sim and fires events. Data flows one way: sim &rarr; render.
</div>

<div class="feature-card" markdown>
### Generic UI Toolkit
ViewModel-based data binding for RTS interfaces. Entity, player, and selection ViewModels auto-refresh each sim tick. Build any UI panel in pure Blueprint.
</div>

</div>

---

## Modules

| Module | Purpose | Status |
|--------|---------|--------|
| **SeinARTSCore** | Fixed-point math, vectors, quaternions, transforms, PRNG | Complete |
| **SeinARTSCoreEntity** | ECS, sim loop, abilities, effects, modifiers, production, squads | Complete |
| **SeinARTSNavigation** | Deterministic grid pathfinding, flowfields, steering | Complete |
| **SeinARTSUIToolkit** | Generic ViewModel-based UI data binding and utilities | Complete |
| **SeinARTSEditor** | Content Browser factory, class picker, editor icons | Complete |
| **SeinARTSFramework** | Player controller, camera, HUD, input bindings | Complete |

## Inspiration

Built with the design sensibility of **Company of Heroes** and **Dawn of War** by Relic Entertainment. Squad-based and individual units, cover systems, terrain types, veterancy, tech upgrades, and resource capture points.

## Quick Links

- [Installation](getting-started/installation.md) — Get the plugin into your project
- [Architecture](getting-started/architecture.md) — Understand the sim/render split
- [UI Toolkit Guide](guides/ui-toolkit.md) — Build your first RTS panel
- [BP Node Reference](reference/index.md) — All Blueprint-exposed functions
