# Production & Tech Nodes

<span class="tag sim">SIM</span> Functions from `USeinProductionBPFL`. These query production availability and tech state — essential for building production panels and tech tree UI.

## Functions

### SeinGetProductionAvailability

```
SeinGetProductionAvailability(PlayerID, BuildingEntity) → TArray<FSeinProductionAvailability>
```

The primary function for building production panels. Returns one entry per producible item on the building, with full availability state:

| Field | Type | Description |
|-------|------|-------------|
| `ActorClass` | `TSubclassOf<ASeinActor>` | The producible archetype |
| `bPrerequisitesMet` | `bool` | Player has required tech tags |
| `bCanAfford` | `bool` | Player has enough resources |
| `bQueueFull` | `bool` | Building's production queue is at capacity |
| `bAlreadyResearched` | `bool` | Research item already completed |

Use this to drive button states in production panels:

```
For each FSeinProductionAvailability:
  if AlreadyResearched → Hide or gray out
  elif not PrerequisitesMet → Disabled state, show "Requires: ..."
  elif not CanAfford → Unaffordable state, red cost text
  elif QueueFull → Disabled state
  else → Available state
```

---

### SeinCanPlayerProduce

```
SeinCanPlayerProduce(PlayerID, ActorClass) → bool
```

Quick boolean check — can this player produce this unit/research? Checks prerequisites and affordability. Use for simple enable/disable without the full availability breakdown.

---

### SeinPlayerHasTechTag

```
SeinPlayerHasTechTag(PlayerID, TechTag) → bool
```

Returns `true` if the player has unlocked the given tech tag. Tech tags are granted when research completes.

---

### SeinGetPlayerTechTags

```
SeinGetPlayerTechTags(PlayerID) → FGameplayTagContainer
```

Returns all unlocked tech tags for a player. Use for tech tree visualization or prerequisite display.

## Production System Overview

### How Production Works

1. Player clicks a production button on a building
2. `QueueProduction` command enters the command buffer
3. Sim validates: prerequisites met? Can afford? Queue not full?
4. If valid: deducts resources, adds to building's queue
5. Each tick: front item's build timer counts down
6. On completion:
   - **Unit**: Entity spawned at rally point, `EntitySpawned` visual event
   - **Research**: Tech tag granted to player, archetype modifiers applied, `TechResearched` visual event

### Cancellation and Refunds

- `CancelProduction` command removes the last queued item
- Resources are fully refunded on cancellation
- Only the last item can be cancelled (LIFO)

### Rally Points

- `SetRallyPoint` command sets where produced units spawn/move to
- Stored on `FSeinProductionComponent`
- Visual rally point marker is render-side only

## Tech Tree

There is no explicit tech tree asset. The tech tree is **emergent** from the prerequisite chains:

```
Building A produces Research X
  → Research X grants TechTag "Tech.Weapons.Tier1"
    → Building B requires "Tech.Weapons.Tier1" to produce Unit Y
```

To visualize the tech tree in UI, walk the archetype definitions and build a dependency graph from `PrerequisiteTags` → `GrantedTechTag` relationships.

## UI Integration

For production panel buttons, use the UI Toolkit's action slot builder:

```
SeinBuildProductionSlotData(BuildingEntity, PlayerID)
  → Returns TArray<FSeinActionSlotData>
```

This packages production items into the same `FSeinActionSlotData` format used by abilities, with `State` reflecting availability (prerequisites, affordability, queue state). See [UI Toolkit Nodes](ui-toolkit.md#action-slot-builders).
