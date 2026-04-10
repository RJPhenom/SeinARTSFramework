# Sim/Render Separation

The sim/render split is the most important architectural concept in SeinARTS. Every design decision flows from it.

## Why Separate?

**Determinism.** Lockstep networking requires that two machines running the same command stream produce identical game state. Unreal Engine's built-in types (`FVector`, `float`, `FMath`) use IEEE 754 floating-point, which can produce different results on different CPUs, compiler settings, or optimization levels.

By isolating the simulation into its own world — one that uses only fixed-point math and deterministic algorithms — we guarantee bit-identical results everywhere.

## The Boundary

```
┌─────────────────────────────────────────────────┐
│  SIMULATION                                      │
│                                                   │
│  FFixedPoint, FFixedVector, FSeinEntityHandle     │
│  Components (USTRUCT), Abilities (USeinAbility)   │
│  USeinWorldSubsystem, FSeinSimContext             │
│                                                   │
│  ✓ Deterministic    ✗ No floats                   │
│  ✓ Fixed-point      ✗ No AActor pointers          │
│  ✓ PRNG only        ✗ No UE random                │
│  ✓ Tick-based       ✗ No frame-rate dependency     │
├─────────────────────────────────────────────────┤
│  BRIDGE                                           │
│                                                   │
│  USeinActorBridgeSubsystem                        │
│  Visual Events (FSeinVisualEvent)                 │
│  Entity → Actor mapping                           │
│                                                   │
├─────────────────────────────────────────────────┤
│  RENDER                                           │
│                                                   │
│  ASeinActor, USeinActorComponent                  │
│  UMG Widgets, Niagara FX, Audio                   │
│  USeinUISubsystem, ViewModels                     │
│                                                   │
│  ✓ Floats OK        ✓ Full UE API                 │
│  ✓ Frame-rate       ✓ Visual interpolation         │
│  ✓ Non-deterministic ✓ Side effects OK             │
└─────────────────────────────────────────────────┘
```

## Rules

### Sim code MUST NOT:

- Use `float`, `double`, `FVector`, `FRotator`, `FQuat`, `FTransform`
- Reference `AActor*`, `UObject*`, or any garbage-collected type
- Call `FMath::Rand()`, `FMath::RandRange()`, or `FMath::Sin()` (non-deterministic transcendentals)
- Access the file system, network, or system clock
- Use Unreal delegates that could fire from render-side code

### Sim code MUST:

- Use `FFixedPoint` for all scalars
- Use `FFixedVector`, `FFixedQuat`, `FFixedTransform` for spatial data
- Use `FSeinEntityHandle` for entity references (never raw pointers)
- Use `FSeinSimContext::GetPRNG()` for random numbers
- Emit `FSeinVisualEvent` when the render layer needs to react

### Render code MUST NOT:

- Write to any sim-side data structure
- Assume sim state is available between ticks (it may be mid-update)

### Render code CAN:

- Read sim data freely (components, attributes, entity state)
- Use any Unreal API (floats, actors, materials, audio, networking)
- Convert sim types to UE types (`FFixedPoint::ToFloat()`, `FFixedVector::ToVector()`)

## Visual Events

The one-way communication channel from sim to render:

```cpp
USTRUCT(BlueprintType)
struct FSeinVisualEvent
{
    GENERATED_BODY()

    UPROPERTY() ESeinVisualEventType Type;
    UPROPERTY() FSeinEntityHandle Source;
    UPROPERTY() FSeinEntityHandle Target;
    UPROPERTY() FFixedPoint Value;
    UPROPERTY() FGameplayTag Tag;
};
```

Events are emitted during the sim tick, buffered, and dispatched after the tick completes. The `USeinActorBridgeSubsystem` routes events to actors and broadcasts `OnVisualEventDispatched` for any system that wants global event access (like the UI).

### Common Event Types

| Event | When | Typical Render Response |
|-------|------|------------------------|
| `EntitySpawned` | New entity created | Spawn actor, play build animation |
| `EntityDestroyed` | Entity killed | Play death animation, delayed cleanup |
| `DamageDealt` | Damage applied | Hit FX, floating damage number |
| `AbilityActivated` | Ability starts | Ability FX, voice line |
| `TechResearched` | Research completes | UI notification, unlock FX |

## Practical Examples

### Reading health for a UI widget (correct)

```
// In a Widget Blueprint:
EntityViewModel → GetResolvedAttribute(FSeinHealthComponent, "CurrentHealth")
  → Returns float (already converted from FFixedPoint)
  → Set progress bar percent
```

### Modifying health from UI (WRONG — never do this)

```
// DON'T: UI code must never write sim state
WorldSubsystem → GetComponentRaw(FSeinHealthComponent)
  → Set CurrentHealth ← This breaks determinism!
```

### The correct way to affect sim state from input

```
// Player clicks "Heal" ability button:
PlayerController → IssueCommand(FSeinCommand::ActivateAbility(HealTag))
  → Command enters the command buffer
  → Sim processes it on next tick
  → Ability activates, modifies health deterministically
```

## Testing Determinism

The framework computes a **state hash** at the end of each sim tick (Post-Tick phase). In a networked game, clients exchange hashes to detect desync. If hashes diverge, something in the sim is non-deterministic.

Common causes of desync:

- Accidentally using `float` math in sim code
- Iterating unordered containers (`TSet`, `TMap` with pointer keys)
- Using `GetWorld()->GetTimeSeconds()` instead of sim tick count
- Calling engine functions that use the FPU internally
