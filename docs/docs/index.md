<div class="sein-overview-wordmark">
	<img src="assets/images/SeinARTSWordmark.svg" alt="SeinARTS Framework" class="sein-hero-wordmark sein-overview-wordmark__image sein-hero-wordmark--dark">
	<img src="assets/images/SeinARTSWordmarkDark.svg" alt="" aria-hidden="true" class="sein-hero-wordmark sein-overview-wordmark__image sein-hero-wordmark--light">
</div>

# Overview

SeinARTS Framework is a deterministic lockstep RTS framework for Unreal Engine 5. It provides the core simulation, entity, navigation, UI, and editor tooling needed to build RTS games with a Blueprint-first workflow.

## What It Includes

- **Deterministic simulation** built on fixed-point math and a deterministic PRNG.
- **Entity/component architecture** for units, abilities, modifiers, production, and squads.
- **Deterministic navigation** with grid pathfinding, flowfields, and steering.
- **Blueprint-first authoring** for gameplay logic, unit setup, and UI.
- **Generic UI toolkit** with ViewModel-based binding utilities for RTS interfaces.

## Modules

| Module                 | Purpose                                                          | Status   |
| ---------------------- | ---------------------------------------------------------------- | -------- |
| **SeinARTSCore**       | Fixed-point math, vectors, quaternions, transforms, PRNG         | Complete |
| **SeinARTSCoreEntity** | ECS, sim loop, abilities, effects, modifiers, production, squads | Complete |
| **SeinARTSNavigation** | Deterministic grid pathfinding, flowfields, steering             | Complete |
| **SeinARTSUIToolkit**  | Generic ViewModel-based UI data binding and utilities            | Complete |
| **SeinARTSEditor**     | Content Browser factory, class picker, editor icons              | Complete |
| **SeinARTSFramework**  | Player controller, camera, HUD, input bindings                   | Complete |

## Start Here

1. [Getting Started Overview](getting-started/index.md)
2. [Installation](getting-started/installation.md)
3. [Architecture](getting-started/architecture.md)
4. [UI Toolkit Guide](guides/ui-toolkit.md)
5. [Reference Overview](reference/index.md)

## Design Principles

**Determinism first.** The simulation is isolated from Unreal's non-deterministic runtime systems so the same inputs produce the same outputs on every machine.

**Blueprint-first workflow.** C++ provides the deterministic primitives, while designers author units, abilities, and UI in Blueprint.

**Composable systems over hardcoded enums.** Abilities, attributes, modifiers, and tags are designed to be assembled into the game-specific rules your RTS needs.
