# Command Brokers

<span class="tag sim">SIM</span> SeinARTS routes every player order through a **CommandBroker** — a sim-side primitive that wraps a member set as a single dispatch target, runs a designer-pluggable resolver, and fans out per-member `ActivateAbility` calls. The framework defines an abstract resolver contract — `USeinCommandBrokerResolver` — and ships a default implementation, `USeinDefaultCommandBrokerResolver`, that handles capability-map filtering and uniform-grid formation positions. Game teams can replace the resolver entirely without touching the rest of the framework.

## Why brokers?

Right-click with 20 heterogeneous units selected (sniper + 2 rifle squads + wheeled vehicle + 2 tanks) on an enemy. Naïve dispatch — every unit gets the same ability against the same target — produces chaos. The broker:

1. Wraps the selection as a single sim entity with one-to-many membership.
2. Polls each member's `FSeinAbilityData` to build an aggregated capability map.
3. Runs a resolver that decides **which members run which abilities at which positions**.
4. Dispatches per-member `ActivateAbility` calls internally — sim-side, NOT logged to the txn buffer (they're deterministic consequences of the one broker order).
5. Tracks all-members-done completion and pops the next queued order, or self-culls.

The transaction log carries **one entry per player click**, regardless of selection size — `FSeinCommand { CommandType = SeinARTS.Command.Type.BrokerOrder, EntityList = members, TargetLocation, Payload = FSeinBrokerOrderPayload }`. Every client's sim spawns or reuses a broker, runs the same resolver, dispatches the same per-member abilities. Lockstep-safe by construction.

## What it does

Per dispatch:

- **Spawns or reuses an abstract entity** carrying `FSeinCommandBrokerData`. No actor, no render presence — the broker lives in the entity pool with a handle, just like any other sim entity.
- **Stamps `FSeinBrokerMembershipData` on each member** so the next dispatch can find-and-evict in O(1) (the "one broker per member" invariant).
- **Filters by ownership**. The member set must all be owned by the issuing `FSeinPlayerID` (single-owner invariant). Match settings can widen this to allied-command-sharing players, but the broker stays single-owner.
- **Caches a capability map** — `TMap<FGameplayTag, TArray<FSeinEntityHandle>>` answering "which members can service each ability tag?" Rebuilt on member-set change; polled fresh per dispatch.
- **Manages the shift-chained order queue**. Shift-clicking with the same selection appends a new order; non-shift clicks replace the queue. Subset shift-clicks (clicking with a subset of a shared broker's members selected) append a subset-targeted order — non-target members keep doing their thing.
- **Drives completion**. Per `FSeinCommandBrokerSystem` (PostTick, priority 40), polls each effective member's primary ability each tick. When all are idle, pops the front order and dispatches the next, or self-culls if the queue is empty.

## Architecture

```
ASeinPlayerController                 (right-click → IssueSmartCommand)
    └─ FSeinCommand { BrokerOrder }   (one txn per click, EntityList = members)
        └─ USeinNetSubsystem          (lockstep wire — every client gets the same command)

USeinWorldSubsystem::ProcessCommands
    └─ CreateBrokerForMembers / append to existing broker queue

FSeinCommandBrokerData                (sim component on the abstract broker entity)
    ├─ Members           TArray<FSeinEntityHandle>
    ├─ ResolverID        int32 → CommandBrokerResolverPool slot
    ├─ Centroid          FFixedVector  (live member centroid, updated each tick)
    ├─ Anchor / Facing   FFixedVector / FFixedQuaternion  (where the formation is trying to stand)
    ├─ CapabilityMap     TMap<FGameplayTag, FSeinBrokerCapabilityBucket>
    └─ OrderQueue        TArray<FSeinBrokerQueuedOrder>

USeinCommandBrokerResolver            (abstract, Blueprintable)
    └─ USeinDefaultCommandBrokerResolver  (shipped default — capability-map dispatch + grid formation)

FSeinCommandBrokerSystem              (PostTick, priority 40)
    ├─ Strips dead members, updates centroid
    ├─ Polls completion → pops front order
    ├─ Dispatches next queued order via the resolver
    └─ Culls empty brokers (no members + no queue)
```

`USeinWorldSubsystem` reads `USeinARTSCoreSettings::DefaultBrokerResolverClass` when a new broker spawns, instantiates that class, and registers it in `CommandBrokerResolverPool`. The component stores an `int32 ResolverID` (not a `TObjectPtr`) so the state hash is portable across processes and snapshots round-trip cleanly.

## When to use what

| Need | API |
|---|---|
| Issue an order from selection (player controller) | `ASeinPlayerController::IssueSmartCommandEx(WorldLocation, TargetActor, bQueue, FormationEnd)` |
| Issue an order from Blueprint / AI | `USeinCommandBrokerBPFL::SeinIssueBrokerOrder` |
| Read which broker an entity is in | `USeinCommandBrokerBPFL::SeinGetCurrentBroker(Member)` |
| Read a broker's live members | `USeinCommandBrokerBPFL::SeinGetBrokerMembers(BrokerHandle)` |
| Read the broker centroid (UI / camera focus) | `USeinCommandBrokerBPFL::SeinGetBrokerCentroid(BrokerHandle)` |
| Read what order is currently dispatching | `USeinCommandBrokerBPFL::SeinGetBrokerActiveOrderContext(BrokerHandle)` |
| Read queue depth (UI shift-queue indicator) | `USeinCommandBrokerBPFL::SeinGetBrokerQueueLength(BrokerHandle)` |

All sim queries take and return `FFixedPoint` / `FFixedVector` / `FSeinEntityHandle` — there's no float on the determinism path.

## The click context

Right-click resolution is **sim-side**, not controller-side. `ASeinPlayerController::BuildCommandContext` translates the cursor hit into a tag container:

| Tag | When |
|---|---|
| `SeinARTS.Command.Context.RightClick` | Always present — the input modality |
| `SeinARTS.Command.Context.Target.Ground` | Ground click (no actor under cursor) |
| `SeinARTS.Command.Context.Target.Friendly` | Hit a same-owner entity |
| `SeinARTS.Command.Context.Target.Neutral` | Hit a neutral-owner entity (resource node, capture point) |
| `SeinARTS.Command.Context.Target.Enemy` | Hit an enemy-owner entity |

The container ships across the wire as the `FSeinBrokerOrderPayload::CommandContext`. Each client's resolver interprets it per-member to pick which ability to activate — see [Default Resolver](default-resolver.md). Designer-added tags (e.g., `Target.Building`, `Target.Damaged`) layer on cleanly: the controller's `BuildCommandContext` is `BlueprintNativeEvent`, so projects can override it to add custom tags without touching the sim path.

## Plugin settings

Configured in **Project Settings → Plugins → SeinARTS → Command Brokers**:

- **Default Broker Resolver Class** — class picker filtered to `USeinCommandBrokerResolver` subclasses. Empty defaults to `USeinDefaultCommandBrokerResolver`. Every broker spawned in this world instantiates this class.

There is no per-volume / per-map / per-faction override surface — the resolver class is project-wide. To vary dispatch behavior by faction or unit type, override `USeinCommandBrokerResolver::ResolveMemberAbility` (the per-member tag pick) rather than swapping resolver classes per broker.

## Pages in this section

- [Default Resolver](default-resolver.md) — what ships: capability-map-filtered dispatch, uniform-grid formation, tag-along behavior for non-combatants
- [Order Lifecycle](order-lifecycle.md) — click → command → spawn/reuse → resolve → tick → completion → cull, plus shift-queue and AutoMoveThen integration
- [Custom Resolvers](custom.md) — building your own `USeinCommandBrokerResolver` (formation shapes, faction-specific dispatch, tight ranks, wedges, class clusters)

## Console commands

Brokers don't ship dedicated console commands. Use `Sein.Net.DumpState` to inspect the current sim state — broker entities show up in the entity count and contribute to the state hash. Per-broker introspection is via the BPFL.

## Module layout

| Module | Owns |
|---|---|
| `SeinARTSCoreEntity` | `USeinCommandBrokerResolver` abstract base, `USeinDefaultCommandBrokerResolver` impl, `FSeinCommandBrokerData` / `FSeinBrokerMembershipData` components, `FSeinBrokerOrderPayload` / `FSeinBrokerDispatchPlan` / `FSeinBrokerQueuedOrder` types, `FSeinCommandBrokerSystem` tick system, `USeinCommandBrokerBPFL`, broker-aware paths inside `USeinWorldSubsystem::ProcessCommands` and `ProcessDeferredDestroys` |
| `SeinARTSFramework` | `ASeinPlayerController::IssueSmartCommandEx` (the gameplay-layer entry point that builds `FSeinBrokerOrderPayload` + emits the `BrokerOrder` command) |
| `SeinARTSNet` | `USeinNetSubsystem::SubmitLocalCommand` (the lockstep wire — broker orders cross the same path as every other command) |

The broker primitive is wholly inside CoreEntity. The framework module's contribution is the click-to-command translation; the net module's is the wire transport. Replacing the resolver class touches only CoreEntity (your subclass) + plugin settings.
