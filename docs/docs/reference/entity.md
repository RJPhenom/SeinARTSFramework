# Entity, Combat, Effect, Player, Squad & Tag Nodes

<span class="tag sim">SIM</span> Functions from the core entity module BPFLs. These operate on sim-side data and use deterministic types.

## Entity Lifecycle

From `USeinEntityBPFL`:

### SeinSpawnEntity

```
SeinSpawnEntity(ActorClass, SpawnTransform, OwnerPlayerID) → FSeinEntityHandle
```

Spawns a new entity from an archetype Blueprint class. Components are copied from the CDO's `USeinArchetypeDefinition`. The returned handle is valid immediately.

---

### SeinDestroyEntity

```
SeinDestroyEntity(EntityHandle) → void
```

Marks an entity for destruction. Actual cleanup occurs during Post-Tick. Emits `EntityDestroyed` visual event.

---

### SeinGetEntityTransform / SeinSetEntityTransform

```
SeinGetEntityTransform(EntityHandle) → FFixedTransform
SeinSetEntityTransform(EntityHandle, Transform) → void
```

Get/set the entity's simulation-space transform. Uses fixed-point types.

---

### SeinGetEntityOwner

```
SeinGetEntityOwner(EntityHandle) → FSeinPlayerID
```

Returns the owning player ID.

---

### SeinIsEntityAlive

```
SeinIsEntityAlive(EntityHandle) → bool
```

Returns `true` if the entity exists and hasn't been destroyed.

---

### SeinIsHandleValid / SeinInvalidHandle

```
SeinIsHandleValid(EntityHandle) → bool
SeinInvalidHandle() → FSeinEntityHandle
```

Validation utilities. `IsHandleValid` checks generation counter. `InvalidHandle` returns a null handle for initialization.

---

## Combat

From `USeinCombatBPFL`:

### SeinApplyDamage

```
SeinApplyDamage(TargetHandle, Damage, SourceHandle, DamageTag) → void
```

Applies damage to target. The `DamageTag` classifies the damage type (for armor calculations, visual FX selection, etc.). Emits `DamageDealt` visual event.

---

### SeinApplyHealing

```
SeinApplyHealing(TargetHandle, Amount, SourceHandle) → void
```

Applies healing. Emits `Healed` visual event.

---

### SeinGetEntitiesInRange

```
SeinGetEntitiesInRange(Origin, Radius, FilterTags) → TArray<FSeinEntityHandle>
```

Spatial query — returns all entities within radius of a point. Optional tag filter (only return entities matching tags). Fixed-point parameters.

---

### SeinGetNearestEntity

```
SeinGetNearestEntity(Origin, Radius, FilterTags) → FSeinEntityHandle
```

Returns the closest entity within radius. Returns invalid handle if none found.

---

### SeinGetEntitiesInBox

```
SeinGetEntitiesInBox(Min, Max, FilterTags) → TArray<FSeinEntityHandle>
```

Spatial query with axis-aligned box bounds.

---

### SeinGetDistanceBetween

```
SeinGetDistanceBetween(EntityA, EntityB) → FFixedPoint
```

Distance between two entities' positions.

---

### SeinGetDirectionTo

```
SeinGetDirectionTo(FromEntity, ToEntity) → FFixedVector
```

Normalized direction vector from one entity to another.

---

## Effects

From `USeinEffectBPFL`:

### SeinApplyEffect

```
SeinApplyEffect(TargetHandle, EffectDefinition, SourceHandle) → int32
```

Applies a timed effect (buff/debuff). Returns an instance ID for later removal. Effects are processed in Pre-Tick (expiration, tick damage/healing).

---

### SeinRemoveEffect

```
SeinRemoveEffect(TargetHandle, EffectInstanceID) → void
```

Removes a specific effect by instance ID.

---

### SeinRemoveEffectsWithTag

```
SeinRemoveEffectsWithTag(TargetHandle, Tag) → void
```

Removes all active effects matching a gameplay tag. Useful for "dispel" abilities.

---

### SeinHasEffectWithTag

```
SeinHasEffectWithTag(TargetHandle, Tag) → bool
```

Checks if entity has any active effect with the given tag.

---

### SeinGetEffectStacks

```
SeinGetEffectStacks(TargetHandle, EffectName) → int32
```

Returns the stack count of a named effect. For stackable buffs/debuffs.

---

## Player

From `USeinPlayerBPFL`:

### SeinGetPlayerResource

```
SeinGetPlayerResource(PlayerID, ResourceName) → FFixedPoint
```

Returns the player's current amount of a named resource (e.g., `"Manpower"`, `"Fuel"`).

---

### SeinAddPlayerResource / SeinDeductPlayerResource

```
SeinAddPlayerResource(PlayerID, ResourceName, Amount) → void
SeinDeductPlayerResource(PlayerID, ResourceName, Amount) → bool
```

Add or deduct resources. `Deduct` returns `false` if the player can't afford it (no partial deduction).

---

### SeinCanPlayerAfford

```
SeinCanPlayerAfford(PlayerID, Cost) → bool
```

Checks if the player can afford a `TMap<FName, FFixedPoint>` cost. Used by production and ability systems.

---

### SeinGetPlayerEntities

```
SeinGetPlayerEntities(PlayerID) → TArray<FSeinEntityHandle>
```

Returns all living entities owned by a player.

---

## Squads

From `USeinSquadBPFL`:

### SeinGetSquadMembers

```
SeinGetSquadMembers(SquadHandle) → TArray<FSeinEntityHandle>
```

Returns handles of all living members in a squad.

---

### SeinGetSquadLeader

```
SeinGetSquadLeader(SquadHandle) → FSeinEntityHandle
```

Returns the squad leader entity.

---

### SeinGetEntitySquad

```
SeinGetEntitySquad(MemberHandle) → FSeinEntityHandle
```

Given a member entity, returns the squad entity it belongs to.

---

### SeinIsSquadMember

```
SeinIsSquadMember(EntityHandle) → bool
```

Returns `true` if the entity is a member of any squad.

---

### SeinGetSquadSize

```
SeinGetSquadSize(SquadHandle) → int32
```

Returns the number of alive members in the squad.

---

## Tags

From `USeinTagBPFL`:

### SeinHasTag / SeinHasAnyTag / SeinHasAllTags

```
SeinHasTag(EntityHandle, Tag) → bool
SeinHasAnyTag(EntityHandle, Tags) → bool
SeinHasAllTags(EntityHandle, Tags) → bool
```

Query an entity's combined tags (base + granted). Uses `FSeinTagComponent::CombinedTags`.

---

### SeinAddTag / SeinRemoveTag

```
SeinAddTag(EntityHandle, Tag) → void
SeinRemoveTag(EntityHandle, Tag) → void
```

Modify an entity's base tags at runtime.
