# Ability Nodes

<span class="tag sim">SIM</span> Functions from `USeinAbilityBPFL`. These control ability activation and provide state queries.

## Functions

### SeinActivateAbility

```
SeinActivateAbility(EntityHandle, AbilityTag, TargetEntity, TargetLocation) → void
```

Activates an ability on an entity by its gameplay tag. Depending on the ability's `TargetType`, provide:

| Target Type | TargetEntity | TargetLocation |
|-------------|-------------|----------------|
| `None` | Ignored | Ignored |
| `Entity` | Required | Ignored |
| `Point` | Ignored | Required |
| `PointOrEntity` | Optional | Fallback |

The system validates cooldown, resource cost, and target validity before activation. If validation fails, the ability doesn't activate (no resources spent).

---

### SeinCancelAbility

```
SeinCancelAbility(EntityHandle) → void
```

Cancels the entity's currently active ability. The ability's latent action chain is interrupted and cleaned up. Use for stop commands, interrupt abilities, or stun effects.

---

### SeinIsAbilityOnCooldown

```
SeinIsAbilityOnCooldown(EntityHandle, AbilityTag) → bool
```

Returns `true` if the ability exists and is currently on cooldown. Returns `false` if the ability doesn't exist on the entity.

---

### SeinGetCooldownRemaining

```
SeinGetCooldownRemaining(EntityHandle, AbilityTag) → FFixedPoint
```

Returns the remaining cooldown time in sim ticks. Returns zero if the ability isn't on cooldown or doesn't exist.

---

### SeinHasAbility

```
SeinHasAbility(EntityHandle, AbilityTag) → bool
```

Returns `true` if the entity has an ability with the given tag in its `FSeinAbilityComponent`.

## UI Integration

For displaying abilities in the UI, prefer the UI Toolkit's action slot builders over querying abilities directly:

```
// Instead of manually querying each ability:
SeinBuildAllAbilitySlotData(Entity)
  → Returns TArray<FSeinActionSlotData> with all display-ready data
```

The action slot builders package ability state (name, icon, cooldown percent, cost, hotkey) into a single struct optimized for UI binding. See [UI Toolkit Nodes](ui-toolkit.md#action-slot-builders).

## Typical Command Flow

```
Player clicks ability button
  → PlayerController issues FSeinCommand{ActivateAbility, Tag, Target}
  → Command buffered until next sim tick
  → Phase 2: Command Processing calls SeinActivateAbility
  → Phase 3: Ability ticks via latent action manager
```

!!! warning "Don't call SeinActivateAbility directly from UI"
    Abilities must go through the command buffer for determinism. The player controller's command system handles this. Direct activation bypasses the buffer and will cause desyncs in multiplayer.
