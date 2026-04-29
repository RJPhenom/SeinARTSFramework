# Default Resolver

<span class="tag sim">SIM</span> The framework ships `USeinDefaultCommandBrokerResolver` — a capability-map-filtered dispatch policy with uniform-grid formation positions. Suitable as a minimal reference and for the "selection of mixed units gets a sane right-click" baseline most RTS projects need.

## What it does

Per dispatch, per effective member:

1. **Pick the ability**. Call `ResolveMemberAbility` (virtual — base impl reads the member's `FSeinAbilityData::ResolveCommandContext`, walking the unit's `DefaultCommands` table).
2. **Primary path**: if the resolved tag is valid and the member owns that ability, dispatch it against the order's target entity / location.
3. **Tag-along path**: if the resolved tag is invalid (or the member doesn't own that ability) and the resolver has a `TagAlongAbility` configured, dispatch *that* ability against the member's formation slot (cohesion, not the order target). Non-combatants tagging along with an attack order, unmapped click types.
4. **Else**: silently skip. Member stays idle for this order.

Members that don't appear in the returned `FSeinBrokerDispatchPlan::MemberDispatches` are simply not dispatched against — they keep doing whatever they were doing (or nothing).

## Per-member ability resolution

`ResolveMemberAbility(World, Member, Context) → FGameplayTag` is a `BlueprintNativeEvent`. The base class implementation delegates to:

```cpp
const FSeinAbilityData* AC = World->GetComponent<FSeinAbilityData>(Member);
return AC->ResolveCommandContext(Context);
```

`FSeinAbilityData::ResolveCommandContext` walks the entity's `DefaultCommands` table — `TArray<FSeinCommandMapping>` authored on each unit's archetype — and picks the highest-priority mapping whose `RequiredContext` is a subset of the actual `Context`. If nothing matches, it falls back to `FSeinAbilityData::FallbackAbilityTag` (typically `SeinARTS.Ability.Move`).

### Authoring `DefaultCommands` on a unit

A medic might author:

| Priority | RequiredContext | AbilityTag |
|---|---|---|
| 100 | `RightClick`, `Target.Friendly`, `Target.Transport` | `Ability.Embark` |
| 50 | `RightClick`, `Target.Friendly` | `Ability.Heal` |
| 50 | `RightClick`, `Target.Enemy` | `Ability.Attack` |
| 0 | `RightClick`, `Target.Ground` | `Ability.Move` |

Plus `FallbackAbilityTag = SeinARTS.Ability.Move` to cover unmapped contexts.

A tank's table would map `Target.Enemy` to `Ability.Attack` and skip the `Heal` / `Embark` rows. When the player right-clicks on a friendly damaged transport with both selected, the resolver fires `Embark` for the medic and falls through to `Move` for the tank — heterogeneous selections work correctly without per-controller branching.

### When to override `ResolveMemberAbility`

Override the hook when picking-the-ability needs context that doesn't fit on a per-unit `DefaultCommands` table:

- **Faction-wide overrides** — "Faction A's workers prefer `Repair` over `Move` when right-clicking a damaged ally; Faction B's workers always prefer `Move`."
- **State-driven overrides** — "An entity carrying a flag prefers `ReturnToBase` over `Attack`."
- **Relationship overrides** — "Allied units treat `Target.Friendly` differently from same-player units."

The override returns an invalid tag to tell the broker "this member can't service this context" — the member falls through to the tag-along path or stays idle.

## Formation positions

`ResolvePositions(Members, Anchor, Facing) → TArray<FFixedVector>` lays out per-member world positions. Default impl produces a uniform square-ish grid centered on the anchor, rotated to face:

```cpp
Side = ceil(sqrt(N))
Spacing = InterUnitSpacing  // 150 cm by default
HalfExtent = (Side - 1) * Spacing / 2

For each i in [0..N):
    Col = i % Side
    Row = i / Side
    LocalOffset = (Row * Spacing - HalfExtent, Col * Spacing - HalfExtent, 0)
    WorldPos = Anchor + Facing.RotateVector(LocalOffset)
```

Returned array is index-aligned with `Members`. The default resolver iterates `EffectiveMembers` (the subset this order targets, or the full member list if the order is unrestricted), calls `ResolvePositions` once, then assigns `Positions[i]` to member `i` for the dispatch.

For a 6-member order, `Side = 3`, `HalfExtent = 150`, layout is a 3×3 grid with the last row partially filled:

```
[0] [1] [2]      — Row 0
[3] [4] [5]      — Row 1
                 — Row 2 (empty for N=6)
```

`Anchor` is the order's `TargetLocation`. `Facing` defaults to identity (no rotation) — projects that want facing-aware grids derive a facing from `Order.FormationEnd - Order.TargetLocation` in a custom resolver. See [Custom Resolvers](custom.md) for the pattern.

## Resolver-level fields

The default resolver exposes two properties on the CDO (Project Settings → Plugins → SeinARTS → pick the resolver class to inspect):

### `InterUnitSpacing` (FFixedPoint)

World-space spacing between units in the uniform grid. UE world units (cm). Default: 150 (≈ one infantryman's personal-space radius). Tune up for vehicles (300–500), down for tight infantry ranks (75–100).

### `TagAlongAbility` (FGameplayTag)

The ability tag dispatched for members whose `ResolveMemberAbility` returned invalid. Targets the formation slot, not the order target — "tag along with the group" semantics. Typical value: `SeinARTS.Ability.Move`.

Set to an invalid tag to disable tag-along entirely. With it disabled, members that can't service the order context stay idle for this order — useful if you want strict "if you can't do this, do nothing" dispatch.

## What the default doesn't do

| Want | Why default doesn't | Solution |
|---|---|---|
| Tight ranks (line formations) | Uniform grid is too loose | Subclass + override `ResolvePositions` |
| Class-clustered spacing (tanks at the back, infantry at the front) | All members get one `InterUnitSpacing` | Subclass + bucket members by archetype tag in `ResolvePositions` |
| Wedge / column / box formation presets | One layout type | Subclass + add a property to pick the layout |
| Different abilities than each member's `DefaultCommands` resolves | Strictly delegates | Override `ResolveMemberAbility` |
| Group-coordinated dispatch (e.g., "any member with `Suppress` fires it; everyone else moves to flank") | Per-member resolution is independent | Override `ResolveDispatch` end-to-end |
| Subset-aware re-resolution (the order targets 3 of 5 members; the other 2 should stay home) | Already handled — `Order.EffectiveMembers` honors subset orders | (No work — see Order Lifecycle) |

For everything but the last row, see [Custom Resolvers](custom.md).

## Subset-targeting semantics

The default resolver iterates `Order.EffectiveMembers` — the subset of broker members this specific order targets — not the broker's full `Members` list. When a player shift-clicks with a strict subset of a shared broker selected, the framework appends a subset-targeted order to the broker's queue; the resolver lays out positions over the subset (so a 1-member "go repair that wall" order places that member at the target, not at slot 3 of a 5-unit grid) and dispatches against the subset only.

Custom resolvers should do the same: iterate `EffectiveMembers`, not `Broker->Members`. Otherwise subset orders dispatch against the wrong set and the broker's completion predicate (which waits on `EffectiveMembers`) hangs.

## State hash + determinism

The default resolver is fully deterministic:

- All math runs on `FFixedPoint` / `FFixedVector` / `FFixedQuaternion`.
- Iteration is in `EffectiveMembers` order — that array is stable across the wire (it's built by the broker system from `Broker->Members`, which is stable).
- `FSeinAbilityData::ResolveCommandContext` walks `DefaultCommands` in priority order — also stable.
- No `FMath::`, no `rand()`, no `FVector`, no float literals on any code path.

A subclass that introduces non-determinism (e.g., calls `FMath::FRand`, captures `GetWorld()->GetTimeSeconds()`, iterates a `TSet`) will desync immediately under lockstep. Run `Sein.Net.DumpState` on every connected window after dispatching an order — same Tick should produce the same StateHash.
