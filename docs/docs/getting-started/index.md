# Getting Started

Welcome to SeinARTS Framework. This section covers everything you need to go from an empty Unreal project to a functioning RTS prototype.

## What You'll Learn

1. **[Installation](installation.md)** — How to add the plugin to your UE5 project and verify the modules load correctly.

2. **[Architecture](architecture.md)** — The mental model behind the framework: the sim/render split, how entities work, the tick lifecycle, and why everything is deterministic.

## Prerequisites

- **Unreal Engine 5.5+** (developed on 5.7)
- Basic familiarity with Unreal Editor and Blueprint
- No C++ required for gameplay authoring — the framework is designed for Blueprint-first workflows

## Philosophy

SeinARTS is built on a few core beliefs:

**Designers build the game, not programmers.** C++ provides deterministic infrastructure — fixed-point math, entity pooling, ability scheduling, pathfinding. But unit definitions, ability logic, damage formulas, tech trees, and UI are all authored in Blueprint. If a designer needs a programmer to add a new unit type, the framework has failed.

**Determinism is non-negotiable.** Every RTS needs lockstep or server-authoritative networking. Both require that the same inputs produce the same outputs on every machine, every time. SeinARTS achieves this through fixed-point math, a deterministic PRNG, and strict isolation of the simulation from Unreal's non-deterministic systems.

**Don't prescribe, enable.** The framework avoids hardcoded enums for commands, damage types, or resource kinds. Instead, it provides generic systems (abilities, modifiers, tags, attributes) that designers compose into whatever their game needs.

## Next Steps

If you're already set up, jump to the [guides](../guides/ui-toolkit.md) to start building.
