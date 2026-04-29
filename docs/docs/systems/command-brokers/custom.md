# Custom Resolvers

<span class="tag advanced">ADVANCED</span> The shipped `USeinDefaultCommandBrokerResolver` handles capability-map-filtered dispatch with uniform-grid positions. For projects that need tight ranks, wedges, class-clustered formations, faction-specific dispatch, group-coordinated dispatch (e.g. "any member with Suppress fires it; everyone else flanks"), or any other variation, `USeinCommandBrokerResolver` is replaceable end-to-end.

This guide walks the full path: subclass, override the hooks you need, register in plugin settings, verify.

## What you implement

Subclass `USeinCommandBrokerResolver`. Override the methods you want to change; the rest stay as the base's defaults.

### Three override hooks

| Method | When called | What it returns | Default impl |
|---|---|---|---|
| `ResolveMemberAbility(World, Member, Context) → FGameplayTag` | Per-member ability pick during dispatch | The ability tag to activate for this member. Invalid tag = "this member can't service this context" | Reads the member's `FSeinAbilityData::ResolveCommandContext` (walks `DefaultCommands` table) |
| `ResolveDispatch(World, BrokerHandle, Order) → FSeinBrokerDispatchPlan` | Once per dispatched order | Plan with one `FSeinBrokerMemberDispatch` tuple per member that should activate | Empty plan (no-op — `USeinDefaultCommandBrokerResolver` overrides this) |
| `ResolvePositions(World, Members, Anchor, Facing) → TArray<FFixedVector>` | Helper called by `ResolveDispatch` for formation layout | Index-aligned with `Members` — each entry is the world position for that member | Returns `Anchor` for every member (no formation) |

All three are `BlueprintNativeEvent`s — implementable in C++ or in a Blueprint event graph. Pick the lowest layer that fits:

- **`ResolveMemberAbility` only** — use this when your override is "different members pick different abilities," but the dispatch shape (one ability per member, target = order target, position = formation slot) stays the same. Faction-wide overrides, state-driven priority shifts.
- **`ResolvePositions` only** — use this when the abilities are right but the layout is wrong. Tight ranks, wedges, class clusters, line formations.
- **`ResolveDispatch` end-to-end** — use this when the dispatch shape itself changes. Group-coordinated dispatch, multi-target attacks, leader-follower patterns where one member's dispatch depends on another's.

### What you DON'T have to implement

- **Broker entity spawn**: `USeinWorldSubsystem::CreateBrokerForMembers` handles eviction, spawn, capability map seeding, membership stamping. You just provide the dispatch policy.
- **Capability map**: rebuilt by `FSeinCommandBrokerSystem` on member-set change. Read it via `Broker->CapabilityMap` if you need it.
- **Tick / completion**: `FSeinCommandBrokerSystem` polls effective members each tick, pops the queue when all are idle, dispatches the next via `ResolveDispatch`. You never write tick logic.
- **Per-member `ActivateAbility`**: the broker system iterates your `Plan.MemberDispatches` and fires `ActivateMemberAbility` per tuple — including cooldown, `CanActivate`, cancel-others, and the `bIsActive` tracking. You return tuples; the system fires them.
- **State hash / snapshots**: your resolver instance is registered in `CommandBrokerResolverPool`. UPROPERTY-tagged fields on your subclass round-trip through the snapshot system automatically via `UClass::SerializeTaggedProperties`.

## Picking your class in settings

After your subclass is registered (UCLASS, UBT picks it up on next compile):

**Project Settings → Plugins → SeinARTS → Command Brokers → Default Broker Resolver Class**

The dropdown filters to `USeinCommandBrokerResolver` subclasses. Pick yours.

`USeinWorldSubsystem::CreateBrokerForMembers` reads this every time it spawns a broker. If empty or stale (class not found), it falls back to `USeinDefaultCommandBrokerResolver`. There is no per-broker / per-faction class override — the resolver class is project-wide. Vary behavior by faction or unit type via overrides inside the resolver, not by swapping classes.

## Pattern 1: Faction-specific ability resolution

Override `ResolveMemberAbility` only. The dispatch shape stays the same; you change which ability fires.

```cpp
// MyResolver.h
#include "Brokers/SeinDefaultCommandBrokerResolver.h"
#include "MyResolver.generated.h"

UCLASS(meta = (DisplayName = "Faction Aware Resolver"))
class MYMODULE_API UMyResolver : public USeinDefaultCommandBrokerResolver
{
    GENERATED_BODY()

public:
    virtual FGameplayTag ResolveMemberAbility_Implementation(
        USeinWorldSubsystem* World,
        FSeinEntityHandle Member,
        const FGameplayTagContainer& Context) override;
};
```

```cpp
// MyResolver.cpp
FGameplayTag UMyResolver::ResolveMemberAbility_Implementation(
    USeinWorldSubsystem* World,
    FSeinEntityHandle Member,
    const FGameplayTagContainer& Context)
{
    // Faction A's workers prefer Repair over Move when right-clicking a damaged
    // friendly. Faction B's workers always prefer Move. Everything else falls
    // back to the base impl (which reads the unit's DefaultCommands table).

    const FSeinTagData* Tags = World->GetComponent<FSeinTagData>(Member);
    if (Tags && Tags->CombinedTags.HasTag(MyTags::Faction_A) &&
        Context.HasTag(SeinARTSTags::Command_Context_Target_Friendly))
    {
        // Check the target's health — only override if damaged
        if (IsTargetDamaged(World, Context))
        {
            return MyTags::Ability_Repair;
        }
    }

    return Super::ResolveMemberAbility_Implementation(World, Member, Context);
}
```

The base class already inherits from `USeinDefaultCommandBrokerResolver`, so the dispatch shape (capability-map filtering, tag-along path, formation positions) carries over. Only the per-member ability pick changes.

## Pattern 2: Class-clustered formation layout

Override `ResolvePositions` only. The abilities are right; the layout shape changes.

```cpp
// ClusteredResolver.h
UCLASS(meta = (DisplayName = "Class Clustered Resolver"))
class MYMODULE_API UClusteredResolver : public USeinDefaultCommandBrokerResolver
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, Category = "SeinARTS|Broker|Formation")
    FFixedPoint InfantrySpacing = FFixedPoint::FromInt(100);

    UPROPERTY(EditDefaultsOnly, Category = "SeinARTS|Broker|Formation")
    FFixedPoint VehicleSpacing = FFixedPoint::FromInt(400);

    UPROPERTY(EditDefaultsOnly, Category = "SeinARTS|Broker|Formation")
    FFixedPoint ClusterSeparation = FFixedPoint::FromInt(800);

    virtual TArray<FFixedVector> ResolvePositions_Implementation(
        USeinWorldSubsystem* World,
        const TArray<FSeinEntityHandle>& Members,
        FFixedVector Anchor,
        FFixedQuaternion Facing) override;
};
```

```cpp
// ClusteredResolver.cpp
TArray<FFixedVector> UClusteredResolver::ResolvePositions_Implementation(
    USeinWorldSubsystem* World,
    const TArray<FSeinEntityHandle>& Members,
    FFixedVector Anchor,
    FFixedQuaternion Facing)
{
    TArray<FFixedVector> Out;
    Out.Init(Anchor, Members.Num());

    // Bucket members by archetype tag (Infantry vs. Vehicle).
    TArray<int32> InfantryIndices;
    TArray<int32> VehicleIndices;
    for (int32 i = 0; i < Members.Num(); ++i)
    {
        if (const FSeinTagData* Tags = World->GetComponent<FSeinTagData>(Members[i]))
        {
            if (Tags->CombinedTags.HasTag(MyTags::Unit_Vehicle))
            {
                VehicleIndices.Add(i);
            }
            else
            {
                InfantryIndices.Add(i);
            }
        }
    }

    // Lay out infantry in a tight grid in front of the anchor; vehicles in a
    // looser grid behind. Both rotated by Facing.
    const FFixedVector InfantryAnchor = Anchor + Facing.RotateVector(
        FFixedVector(ClusterSeparation, FFixedPoint::Zero, FFixedPoint::Zero));
    const FFixedVector VehicleAnchor = Anchor - Facing.RotateVector(
        FFixedVector(ClusterSeparation, FFixedPoint::Zero, FFixedPoint::Zero));

    PlaceInGrid(InfantryIndices, InfantryAnchor, Facing, InfantrySpacing, Out);
    PlaceInGrid(VehicleIndices, VehicleAnchor, Facing, VehicleSpacing, Out);

    return Out;
}
```

`PlaceInGrid` is a private helper that mirrors the default resolver's grid math but writes into `Out` at the given indices. Out is index-aligned with the original `Members` array, so the caller's per-member dispatch can map position → member directly.

## Pattern 3: Group-coordinated dispatch

Override `ResolveDispatch` end-to-end. The dispatch shape itself is policy.

```cpp
// SuppressFlankResolver.h
UCLASS(meta = (DisplayName = "Suppress And Flank Resolver"))
class MYMODULE_API USuppressFlankResolver : public USeinCommandBrokerResolver
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, Category = "SeinARTS|Broker|Tactics",
        meta = (Categories = "SeinARTS.Ability"))
    FGameplayTag SuppressTag;

    UPROPERTY(EditDefaultsOnly, Category = "SeinARTS|Broker|Tactics",
        meta = (Categories = "SeinARTS.Ability"))
    FGameplayTag AttackTag;

    UPROPERTY(EditDefaultsOnly, Category = "SeinARTS|Broker|Tactics")
    FFixedPoint FlankRadius = FFixedPoint::FromInt(800);

    virtual FSeinBrokerDispatchPlan ResolveDispatch_Implementation(
        USeinWorldSubsystem* World,
        FSeinEntityHandle BrokerHandle,
        const FSeinBrokerOrderInput& Order) override;
};
```

```cpp
// SuppressFlankResolver.cpp
FSeinBrokerDispatchPlan USuppressFlankResolver::ResolveDispatch_Implementation(
    USeinWorldSubsystem* World,
    FSeinEntityHandle BrokerHandle,
    const FSeinBrokerOrderInput& Order)
{
    FSeinBrokerDispatchPlan Plan;
    if (!World) return Plan;

    // Only kick in for attack orders. Fall back to default for anything else.
    if (!Order.Context.HasTag(SeinARTSTags::Command_Context_Target_Enemy))
    {
        return Super::ResolveDispatch_Implementation(World, BrokerHandle, Order);
    }

    // Pick the first member with Suppress to fire it. Everyone else gets a
    // flanking move position around the target.
    FSeinEntityHandle SuppressMember;
    TArray<FSeinEntityHandle> Flankers;
    for (const FSeinEntityHandle& M : Order.EffectiveMembers)
    {
        const FSeinAbilityData* AC = World->GetComponent<FSeinAbilityData>(M);
        if (!AC) continue;

        if (!SuppressMember.IsValid() && AC->HasAbilityWithTag(*World, SuppressTag))
        {
            SuppressMember = M;
        }
        else
        {
            Flankers.Add(M);
        }
    }

    // Suppress dispatch — at the order target.
    if (SuppressMember.IsValid())
    {
        FSeinBrokerMemberDispatch MD;
        MD.Member         = SuppressMember;
        MD.AbilityTag     = SuppressTag;
        MD.TargetEntity   = Order.TargetEntity;
        MD.TargetLocation = Order.TargetLocation;
        Plan.MemberDispatches.Add(MD);
    }

    // Flanking dispatches — Attack ability against the target, but each member
    // moves toward a position offset by FlankRadius from the target's centroid.
    for (int32 i = 0; i < Flankers.Num(); ++i)
    {
        const FFixedPoint Angle = (FFixedPoint::TwoPi * FFixedPoint::FromInt(i))
            / FFixedPoint::FromInt(Flankers.Num());
        const FFixedVector FlankPos = Order.TargetLocation + FFixedVector(
            FlankRadius * SeinMath::Cos(Angle),
            FlankRadius * SeinMath::Sin(Angle),
            FFixedPoint::Zero
        );

        FSeinBrokerMemberDispatch MD;
        MD.Member         = Flankers[i];
        MD.AbilityTag     = AttackTag;
        MD.TargetEntity   = Order.TargetEntity;
        MD.TargetLocation = FlankPos;
        Plan.MemberDispatches.Add(MD);
    }

    return Plan;
}
```

This pattern decouples ability pick from formation pick from dispatch shape. Members that don't appear in `Plan.MemberDispatches` are silently skipped — the broker's completion predicate still waits on `Order.EffectiveMembers`, so a member you skip but include in the effective set will hang the queue. If you want a member to be fully ignored for an order, exclude it from `EffectiveMembers` (set `Order.TargetMembers` upstream) rather than just skipping it in dispatch.

## Pattern 4: Blueprint-only resolver

A pure-Blueprint subclass works fine for prototyping. The three hooks are `BlueprintNativeEvent`s, so they appear as overridable events in the BP class settings.

1. **Right-click → Create Blueprint Class → Pick Parent → All Classes → search "Command Broker Resolver"**. Pick `USeinCommandBrokerResolver` (or `USeinDefaultCommandBrokerResolver` to inherit the default dispatch shape).
2. Open the new BP. **My Blueprint → Override** dropdown. Pick `Resolve Member Ability`, `Resolve Dispatch`, or `Resolve Positions`.
3. Implement in the event graph. The `World` pin is a `USeinWorldSubsystem*` — pass it to BPFL nodes that need world context. The `Order` pin is a `FSeinBrokerOrderInput` struct — break it to read context, target, effective members.
4. Save. Open **Project Settings → Plugins → SeinARTS → Command Brokers → Default Broker Resolver Class**. Pick your BP.

The same fixed-point determinism contract applies: don't call `FMath::FRand`, don't capture wall-clock time, don't iterate `TSet`s of `UObject*`s. The framework's BP-side fixed-point library is in [Fixed-Point Math](../../concepts/fixed-point.md).

## Build dependencies for your custom module

If you ship the resolver as a separate plugin module:

```csharp
PrivateDependencyModuleNames.AddRange(new string[] {
    "Core", "CoreUObject", "Engine",
    "GameplayTags",
    "SeinARTSCore",          // FFixedPoint, FFixedVector, FFixedQuaternion
    "SeinARTSCoreEntity"     // USeinCommandBrokerResolver, FSeinBrokerOrderInput,
                             // FSeinBrokerDispatchPlan, FSeinCommandBrokerData,
                             // FSeinAbilityData, USeinWorldSubsystem
});
```

You don't need a dependency on `SeinARTSFramework` (the player controller emits the broker order, but your resolver never sees the controller — only the sim-side command). You don't need `SeinARTSNet` either; broker orders cross the wire as ordinary `FSeinCommand`s.

## Things to verify

After your subclass is in plugin settings:

- [ ] Issue an order from PIE. `SeinGetBrokerActiveOrderContext(BrokerHandle)` returns the click context tags. If empty, the dispatch never fired — check `World.GetCommandBrokerResolver(Broker.ResolverID)` returns your class, not the default.
- [ ] Members move / fire as your resolver intends. If only a subset of effective members dispatches, check that every member you want to act has an entry in `Plan.MemberDispatches`.
- [ ] After all members complete, `SeinGetBrokerQueueLength` returns 0 and the broker is gone within one PostTick. If it hangs, an effective member never deactivated their ability — re-check the BP graph wires `End Ability` on every Completed / Failed / Cancelled pin.
- [ ] Shift-click queues. `SeinGetBrokerQueueLength` increments per shift-click; the front order pops on completion and the next dispatches.
- [ ] Subset shift-click. With three of five members selected, shift-click — `SeinGetBrokerQueueLength` increments by 1, and only the three selected members dispatch when the new order pops.
- [ ] `Sein.Net.DumpState` after dispatch. Same Tick → same StateHash on every connected window. A divergence after your resolver runs points at non-deterministic state (float math, `TSet<UObject*>`, captured world-time, `FMath::Rand`).
- [ ] `Sein.Net.DumpSnapshot` + `Sein.Net.LoadSnapshot` round-trip. Your resolver's UPROPERTY fields should round-trip via tagged-property serialization automatically. If a field doesn't round-trip, check it's `UPROPERTY()` (with or without other specifiers).

## When NOT to replace the default

If your project's needs map to "right-click does the obvious thing for each unit, formation is roughly grid-shaped, vehicles space themselves a bit further apart" — `USeinDefaultCommandBrokerResolver` plus a tuned `InterUnitSpacing` plus per-archetype `DefaultCommands` tables covers it. Custom resolvers are a multi-week investment with a long determinism tail; try to push the default as far as it goes first.

If you DO need to replace it, the abstract surface is the boundary. Nothing in `SeinARTSFramework` (player controller, HUD, selection), `SeinARTSNet` (lockstep transport), or other CoreEntity primitives (abilities, effects, production) should ever need to change — the broker primitive consumes and produces `FSeinCommand` / `FSeinBrokerOrderInput` / `FSeinBrokerDispatchPlan`, and those shapes are stable.
