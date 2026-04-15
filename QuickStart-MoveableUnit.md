# Quick-Start: Getting a Right-Click-Moveable Unit in Editor

## Recap: What's Built

You have a full deterministic RTS pipeline end-to-end:

- **Input** — `ASeinPlayerController` handles right-click, traces the world, builds a context tag set (`{RightClick, Target.Ground}`), resolves it against the archetype's `DefaultCommands` to pick an ability tag, and enqueues a `FSeinCommand` into the sim.
- **Sim** — `USeinWorldSubsystem` processes commands each tick → activates the matched ability (e.g. `USeinAbility` subclass for movement) → `USeinMoveToAction` pathfinds and advances the entity each sim tick using `FSeinMovementComponent::MoveSpeed`.
- **Render** — `USeinActorBridgeSubsystem` listens to `OnSimTickCompleted`, syncs transform snapshots to `ASeinActor` instances, and auto-spawns actors when it sees `EntitySpawned` visual events.

---

## Quick-Start: Getting a Right-Click-Moveable Unit in Editor

### Step 1 — Create the Movement Ability Blueprint
1. Content Browser → **Add** → pick the **Sein Ability** factory → name it `BP_Ability_MoveTo`
2. Parent class: `USeinAbility`
3. In its defaults, set the **Ability Tag** to `Ability.Movement`
4. In `OnActivate`, create a `USeinMoveToAction` latent action using the command's `TargetLocation`
5. The action handles pathfinding + per-tick movement automatically

### Step 2 — Create the Unit Blueprint
1. Content Browser → **Add** → pick the **Sein Actor** factory → name it `BP_Unit_Basic`
2. Parent class: `ASeinActor`
3. Add a visible mesh (cube/capsule/skeletal) so you can see it
4. On the **USeinArchetypeDefinition** component (already on the CDO):
   - **Components** array — add entries for:
     - `FSeinMovementComponent` — set `MoveSpeed` (e.g. `FFixedPoint(5.0)` or whatever the editor exposes)
     - `FSeinAbilityComponent` — add `BP_Ability_MoveTo` to `GrantedAbilityClasses`
     - `FSeinTagComponent` — set `ArchetypeTag` to something like `Unit.Basic`
   - **DefaultCommands** — add a mapping:
     - Context tags: `RightClick`, `Target.Ground`
     - Ability Tag: `Ability.Movement`

### Step 3 — Spawn It
- Place/spawn a `BP_Unit_Basic` in your level, or use `USeinWorldSubsystem::SpawnEntity()` to create a sim entity (the actor bridge will auto-spawn the actor)

### Step 4 — Play
- Select the unit (left-click / marquee)
- Right-click on the ground
- The controller resolves `{RightClick, Target.Ground}` → `Ability.Movement`, enqueues the command, sim activates the move ability, and the actor bridge syncs the position each frame

---

The main things to watch for: make sure the **Ability Tag** on the BP matches what's in the **DefaultCommands** mapping, and that the `FSeinMovementComponent` has a non-zero `MoveSpeed`. Everything else is wired up in C++ already.
