# Attribute & Modifier Nodes

<span class="tag sim">SIM</span> Functions from `USeinAttributeBPFL`. The attribute system uses FProperty reflection to read and write fields on any component struct, with full modifier support.

## How Attributes Work

An "attribute" is any `UPROPERTY` field on a component struct. The system doesn't require a special base class or registration — if it's a `UPROPERTY`, it's an attribute.

**Resolution order:**

1. **Base value** — the raw field value on the component
2. **Archetype modifiers** — per-player modifiers that apply to all entities of a type (e.g., "all Riflemen get +10% damage after tech upgrade")
3. **Instance modifiers** — modifiers on this specific entity (e.g., veterancy bonuses, active buffs)

The resolved value is: `Base → apply archetype mods → apply instance mods → final value`

## Functions

### SeinResolveAttribute

```
SeinResolveAttribute(EntityHandle, ComponentType, FieldName) → FFixedPoint
```

Returns the fully resolved value of a named field, including all active modifiers. This is the function you should use for gameplay logic (damage calculations, movement speed, etc.) and UI display.

**Parameters:**

| Parameter | Type | Example |
|-----------|------|---------|
| EntityHandle | FSeinEntityHandle | The entity to query |
| ComponentType | UScriptStruct* | `FSeinHealthComponent::StaticStruct()` |
| FieldName | FName | `"MaxHealth"` |

**Example — reading resolved max health:**

```
SeinResolveAttribute(MyEntity, FSeinHealthComponent, "MaxHealth")
  → Returns 1320.0 (base 1200 + 10% veterancy bonus)
```

---

### SeinGetBaseAttribute

```
SeinGetBaseAttribute(EntityHandle, ComponentType, FieldName) → FFixedPoint
```

Returns the raw base value *before* any modifiers. Use for:

- Tooltip comparisons ("Base: 1200, Modified: 1320")
- Checking the unmodified value for game logic that shouldn't stack

---

### SeinSetBaseAttribute

```
SeinSetBaseAttribute(EntityHandle, ComponentType, FieldName, Value) → void
```

Sets the base value of a field directly. Use sparingly — most value changes should go through modifiers or effects so they can be tracked and reverted.

## Modifiers

Modifiers are not applied through the BPFL directly — they're managed by the effect system and archetype modifier system. But understanding them is key to using attributes:

### Instance Modifiers

Applied to a single entity. Typically created by effects (buffs/debuffs):

- **Additive**: Add a flat value (e.g., +50 health)
- **Multiplicative**: Multiply by a factor (e.g., ×1.1 for +10%)

### Archetype Modifiers

Applied to all entities of a type, scoped per player. Created by tech research:

- When a player researches "Weapon Upgrade Tier 1", it grants archetype modifiers
- All that player's Riflemen get the bonus automatically
- New Riflemen spawned after the research also get it

Archetype modifiers are stored on `FSeinPlayerState::ArchetypeModifiers` and resolved by matching `ComponentType + FieldName + ArchetypeTag`.

## Usage in UI

From the UI ViewModel (render-side):

```
EntityViewModel → GetResolvedAttribute(ComponentType, FieldName) → float
EntityViewModel → GetBaseAttribute(ComponentType, FieldName) → float
```

These return `float` (already converted from `FFixedPoint`) since you're on the render side.

!!! tip "Show the modifier breakdown in tooltips"
    Display both base and resolved values to help players understand what modifiers are active:
    ```
    Damage: 85 (Base: 70 + 15 from Weapon Upgrade)
    ```
    Calculate the difference: `Resolved - Base = modifier total`.
