# SeinARTS Framework — Design Reference

A living reference for the design of SeinARTS's core simulation primitives and their interactions. Not a roadmap — a "how does this system work and why" document that stays accurate as implementation evolves.

**Audience:** the designer/author (RJ), future contributors, future Claude sessions. Paired with [CLAUDE.md](CLAUDE.md) (orientation-for-assistants) and the code itself (source of truth).

**Scope:** the sim-layer primitives and the systems that span them. Render/editor/UI concerns appear only where they cross the sim boundary.

**Inspiration target:** Company of Heroes, Total War, Dawn of War, StarCraft 2, Age of Empires, Supreme Commander. Most shared DNA, variance at the edges.

---

## Status legend

Each section carries a status tag so we know where in the design pass it is:

- **`[open]`** — no decisions yet, placeholder
- **`[drafting]`** — Q&A in progress, decisions accumulating
- **`[locked]`** — design decisions settled; implementation may or may not be done
- **`[re-opened]`** — decisions being revisited; previous contents archived inline

---

## Cross-cutting invariants `[locked]`

Every primitive and system in this document must satisfy these. A design that fights them is rejected.

1. **Sim/render separation is absolute.** The simulation runs on fixed-point math (`FFixedPoint`, `FFixedVector`, etc.) and deterministic primitives. It never reads `AActor*`, `FVector`, `FMath::`, `float` field values, wall-clock time, or non-deterministic RNG. The render/editor/UI layer reads from the sim and reacts to visual events. Data flows one way: sim → render.

2. **Lockstep-compatible.** Only `FSeinCommand` entries cross the network. All sim state is derived from the sequence of commands applied to a known initial state. Clients stay bit-identical tick-by-tick. Observer-only commands (camera moves, selection changes) may enter the command log for replay reconstruction but produce no sim-state mutation.

3. **Replay-serializable.** A match replay is a command stream plus an initial RNG seed. Replaying feeds commands at their recorded ticks and re-runs the sim. State-hash checks at each tick validate determinism. This implies: no primitive stores transient non-deterministic data; every piece of sim state is either re-derivable from commands or serializable into a frame snapshot.

4. **Tick phase must be declared.** Every system that mutates sim state runs at a specific phase (PreTick / CommandProcessing / AbilityExecution / PostTick) with a known priority. No ad-hoc mutation from outside the tick pipeline. Exception: `ProcessCommands` runs inside CommandProcessing and dispatches ability activations synchronously.

5. **Tag integration.** When a system exposes state that other systems might want to query positionally or conditionally, it should expose that state as tags on the entity (via `FSeinTagData`). Other systems read tags, not direct component fields. Keeps coupling loose and designer-editable queries possible.

6. **No premature replication abstractions.** We are building a deterministic lockstep framework, not a traditional client-server replication framework. Replication lives in Unreal's built-in networking stack for lobby/chat/handshake only. Sim state is never replicated via `UPROPERTY(Replicated)` because it's deterministically recomputed.

7. **Designer-editable where possible.** If a field can reasonably be authored in Blueprint defaults rather than hardcoded in C++, it should be. The framework provides primitives; the game's content authors compose them.

---

## 1. Entities `[open]`

The atomic unit of sim existence. Anything with a position (or sim-relevant abstract presence), an owner, and a handle.

**Open questions for Q&A:**
- Does "entity" mean "anything with sim state" or "anything with a position"? (Squads are abstract entities with no transform; the framework already treats them as entities.)
- Are resource points entities? Capturable terrain features? Projectiles?
- What's the minimum component set an entity needs to exist?

---

## 2. Tags `[open]`

Gameplay tags on entities. Used for arbitration, queries, classification, and cross-system communication.

**Open questions for Q&A:**
- `BaseTags` (archetype-granted) vs `GrantedTags` (runtime-added) — the split works, but should archetype-granted tags be immutable? Can research remove them?
- Tag namespaces and conventions (`Unit.Infantry.Sniper`, `Ability.State.Channeling`, `Terrain.Cover.Heavy`)?
- Blocking/query tag containers on entities (like GAS) for passive cross-system checks?

---

## 3. Commands `[open]`

Player/AI intent. A command says "this player wants this entity to do this thing." The ability system resolves what "this thing" maps to.

**Open questions for Q&A:**
- Smart commands (right-click chooses Move vs Attack vs Harvest by target): who decides? The player controller, the archetype, or the ability self-declaration?
- Command queues (shift-click to chain orders): sim-side queue on the entity, or client-side batching?
- Formation commands (line up in X shape): a command type or emergent from per-entity commands?
- Observer commands (camera, selection): already noted as logged-for-replay but non-sim. Lock in.

---

## 4. Resources `[open]`

Player-level economy. Manpower/munitions/fuel (CoH), minerals/gas/pop (SC2), mass/energy (SupCom flow), wood/food/gold/stone (AoE), army upkeep (TW).

**Open questions for Q&A:**
- Stockpile vs flow semantics (SupCom mass/energy is flow with buffer cap; CoH is pure stockpile). Designer-configurable per resource?
- Income sources: cap points, structures, passive, abilities, upkeep drains. All go through the same `AddResources`/`DeductResources` API?
- Pop cap as a "soft" resource vs a distinct system?
- Shared resources / allied transfers?
- Caps and scarcity behavior (hard cap, soft overflow, decay)?

---

## 5. Abilities `[drafting]`

The unit's implementation of commands. Activated explicitly or passively, with tag-based arbitration (already locked), latent-action execution (already working), and BP-scriptable lifecycle.

**Already decided** (as of cleanup chapter):
- Three-container arbitration: `ActivationBlockedTags`, `OwnedTags`, `CancelAbilitiesWithTag`
- Diff-on-activate for `OwnedTags` (preserves archetype-granted tags)
- Latent actions torn down automatically on deactivate
- BP authors wire `End Ability` on terminal pins

**Open questions for Q&A:**
- `ResourceCost` currently inert — wire to sim? Auto-deduct on activate, auto-refund on cancel? Punitive-cancel toggle per ability?
- Cooldown rollback on cancel — default reset, default preserve, designer toggle?
- Move `DefaultCommands` + `FallbackAbilityTag` from archetype to `USeinAbilitiesComponent` (we agreed; this is ratification).
- Interrupt vs abort distinction — is "cancelled by another ability" different from "player-cancelled" different from "failed"?

---

## 6. Effects `[open]`

Runtime modifiers applied to entities or player states. Health regen, damage-over-time, stat buffs, production discounts.

**Open questions for Q&A:**
- Stacking rules: refresh, add, replace, per-source? Per-effect?
- Duration model: fixed, instant, infinite-until-removed?
- Source-death: does an effect persist when the entity that applied it dies?
- Archetype-scope effects (via `FSeinPlayerState::ArchetypeModifiers`) vs instance-scope — unified or separate data paths?
- Effects modifying BP-editor-authored custom component fields — does the reflection path handle this cleanly today?

---

## 7. Production `[open]`

Unit production, research production, upgrade production. Currently wired for unit/research via `FSeinProductionData`. Cost deducted at enqueue, refunded at cancel — one mental model we want abilities to match.

**Open questions for Q&A:**
- Cost deduction timing: at enqueue (current) vs at start-of-build vs at completion? Consistent with ability-cost model.
- Queue semantics: FIFO only, or priority/insert?
- Mid-build cancel refund: full (current), partial (proportional), none (punitive), designer-toggle?
- Production-cancelled events affect the produced unit's tech grants? (Research specifically.)
- Buildings producing inside themselves (SupCom factories) — same system or special-case?

---

## 8. Tech / Upgrades `[open]`

Tech tags granted to players via research. Unlocks prerequisites, stacks archetype modifiers. Upgrades are per-unit or per-faction permanent stat/ability modifications.

**Open questions for Q&A:**
- Revocable tech? (CoH commander swap, era regression in strategy games.)
- Per-unit upgrades (squad gets a rifle attachment) vs per-player tech (faction unlocks Tier 3) — same primitive or different?
- Stacking: can a tech be researched multiple times (+10% damage each stack)?
- Faction-scoped: tech only visible to certain factions, or global unlockable?

---

## 9. Combat pipeline `[open]`

Damage resolution: attacker stats → accuracy → armor → damage type → final health delta. Rides on top of abilities + effects + attributes + tags. Not its own primitive; a well-defined flow through the existing ones.

**Open questions for Q&A:**
- Damage types (small arms, AT, HE, piercing, etc.) — designer-enumerated or hardcoded?
- Armor model: flat, typed (front/side/rear), or gameplay-tag-matched?
- Accuracy resolution: deterministic RNG (rolls against sim PRNG) or fully deterministic hit-probability math?
- Friendly fire, splash, collateral — opt-in per ability?

---

## 10. Vision / LOS / Fog `[open]`

Per-player or per-team fog-of-war state, runtime-mutable. Stealth/detection mechanics, shared vision via alliances or tech.

**Decisions aligned:**
- Mode (player vs team) is runtime-mutable state on the player, not a hardcoded constant. A tech/ability can toggle it.
- Fog computation is deterministic per-player; what the local player renders is a read off that shared state.

**Open questions for Q&A:**
- Vision source types: unit radius, building radius, scan ability, spotter relationships?
- "Was-seen" vs "currently-seen" vs "never-seen" distinction (explored-but-fogged is standard; some RTS omit it)?
- Stealth/detection: tag-based, component-based, both?
- How does fog gate commands? Can the player click-target an enemy they can see through an ally's vision?

---

## 11. Terrain / Environment `[open]`

Positional sim state that isn't an entity. Cover, elevation, biome, garrison points, capture points.

**Open questions for Q&A:**
- Does terrain live on the navigation grid, or as a separate sim-wide layer?
- Cover: discrete levels (green/yellow) or continuous (percentage)?
- Elevation: discrete tiers or continuous? Affects combat math how?
- Designer authoring: placed-in-editor volumes, or baked from the nav grid?

---

## 12. Relationships (containment, transport, garrison, attachment) `[open]`

Entity-to-entity edges that aren't squad membership. Infantry garrisoned in building. Squad loaded into transport. Hero attached to TW regiment. Crew operating a weapon.

**Open questions for Q&A:**
- Single generalized "relationship" system, or separate systems per relationship type?
- Can an entity be in multiple relationships (infantry in transport + part of squad)?
- How does a contained entity interact with the sim? Hidden, exposed, partial?
- Capacity, eviction, destruction propagation?

---

## 13. Selection & control groups `[open]`

Client-side ephemeral state. Who is currently selected, what control groups are bound. Not sim. Logged to the command log as observer commands so that replays can rebuild the UI state of the original player POV.

**Decisions aligned:**
- `SelectionChanged` and `ControlGroupAssigned`-style commands are observer commands — logged, not processed sim-side.
- Replay can reconstruct "what was the player looking at when X happened" from the command log.

**Open questions for Q&A:**
- Per-client or per-player (if one player controls multiple units across multiple seats in co-op)?
- Box-select, double-click-all-of-type, type-cycling hotkeys — all same subsystem?
- Persistence across match-session (save/restore selection between games)?

---

## 14. AI `[open]`

Server/host-resident command issuer. Issues `FSeinCommand`s the same way a human player controller would. Replays and multiplayer-in-lobby work automatically because AI commands flow through the same lockstep buffer.

**Decisions aligned:**
- AI is a player-equivalent command issuer, not a special sim primitive.
- Runs on the host in networked games; runs locally in single-player.

**Open questions for Q&A:**
- Decision-making scope: behavior tree, state machine, utility AI, scripted?
- How does AI query sim state? Same read APIs as the UI layer, or a dedicated AI "blackboard" abstraction?
- Difficulty scaling — via modifiers on the AI's issued commands (e.g., lower APM), via per-difficulty resource bonuses, or both?
- Faction-specific AI personalities?

---

## 15. Scenario events / cinematics `[open]`

Co-op campaign scripting. "When X dies, spawn Y and reveal Z." "Cinematic at tick N triggers dialogue and camera pan." Must play in sync across all co-op clients.

**Decisions aligned:**
- Scripted events must play deterministically in sync across co-op clients. Implies: either they ride on the sim (sim state changes trigger them), or they are themselves commands in the lockstep stream.
- Cinematics in particular need deterministic trigger points + synchronized playback.

**Open questions for Q&A:**
- Authoring model: Blueprint graph, dedicated asset (`USeinScenario`), or in-level actors with event volumes?
- Runtime: a dedicated subsystem that ticks with the sim, reading sim state and emitting commands/visual events? Or a set of abilities granted to a scenario-owner entity?
- Interaction with replay: scenario scripts should replay identically because their inputs (sim state) replay identically.
- Mid-mission save/resume (if that's on the roadmap)?

---

## Open cross-system questions (to resolve during Q&A)

- **Default component set.** Tabled for now per discussion — revisit if composition cost becomes a pain point.
- **Reference counting for shared tag state.** If multiple abilities own the same tag, diff-on-activate has a known limitation (first-stripper wins). Defer unless we hit it in practice.
- **Whether `USeinArchetypeDefinition` itself should shrink** as things like `DefaultCommands` move off it. Likely yes — it ends up carrying identity, cost, tech metadata, and archetype-scope modifier declarations only.

---

## Changelog

- **2026-04-17** — Initial scaffold, cross-cutting invariants locked, 15 sections staked out. Abilities partially drafted (arbitration model and latent cleanup already implemented during the preceding cleanup chapter).
