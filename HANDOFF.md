# SeinARTS Framework — Session Handoff

**Date**: 2026-04-22
**Handoff from**: design-pass parent → implementation-audit parent → next-parent
**For**: the session(s) that pick up after the design-pass + Phase 0–5 implementation + audit + determinism fixes are all landed.

This doc is the **parent-to-parent handoff**. Read it end-to-end before opening a child session. It covers what's done, what the state of the codebase is, where the sharp edges are, and the three priorities set for the next major run.

Complements (don't replace) these existing docs:
- **[CLAUDE.md](CLAUDE.md)** — framework orientation, conventions, pitfalls
- **[DESIGN.md](DESIGN.md)** — authoritative per-primitive spec (17 sections, all `[locked]`)
- **[PLAN.md](PLAN.md)** — phased implementation log (Phase 0–5 all `[COMPLETE]` + integration pass)

---

## 1. Where we are

**Framework MVP is code-complete and building clean.** All 17 DESIGN.md sections are locked; all 5 implementation phases (Phase 0 IP scrub → Phase 5 session layer) are done plus a follow-up integration pass that closed the deferred cross-system wiring (friendly-fire consults diplomacy, rally auto-move via broker, SharedVision aggregation, PC → BrokerOrder migration).

**This session's work** (2026-04-22) focused on:
1. Review the codebase against DESIGN.md using parallel-agent audits.
2. **Fix three real determinism threats** found by the audits (details below).
3. Produce this handoff doc.

No code was written for this session beyond the determinism fixes. User instruction was explicit: review + fix determinism threats + handoff only.

---

## 2. What just got fixed (2026-04-22)

Three real desync threats were found and fixed. Networked builds of this framework must be bit-identical across clients given the same command stream and PRNG seed; each of the below would have caused silent divergence.

### Fix #1 — Vision subsystem wall-clock tick (CRITICAL)

**Problem.** `USeinVisionSubsystem` was a `FTickableGameObject` with a `float TickAccumulator` driven by wall-clock `DeltaTime`. It ran vision updates at ~10 Hz wall-clock. Because vision state feeds the line-of-sight resolver (via `FSeinLineOfSightResolver`) and thus ability activation gates (via `FSeinAbilityValidation::ValidateTarget`), two clients at slightly different wall-clock phases would compute different vision state when a sim command queried LOS. Any fog-of-war-gated command would silently desync.

**Fix.** Removed `FTickableGameObject` base. Subsystem now binds `USeinWorldSubsystem::OnSimTickCompleted` in `OnWorldBeginPlay`, unbinds in `Deinitialize`. Ticks vision every N sim ticks via `CurrentSimTick % SimTickInterval == 0` (default N=3 → ~10 Hz at 30 Hz sim). All clients reach the same sim tick at the same modulo phase. Sim pause naturally pauses vision because the delegate doesn't fire.

**Files**:
- `Source/SeinARTSNavigation/Public/Vision/SeinVisionSubsystem.h` — removed FTickableGameObject methods, added `HandleSimTickCompleted` + `SimTickInterval` constant + `SimTickDelegateHandle`. Added a prominent header comment explaining the determinism rationale.
- `Source/SeinARTSNavigation/Private/Vision/SeinVisionSubsystem.cpp` — bind/unbind in `OnWorldBeginPlay`/`Deinitialize`, delete `Tick(float)` + `GetStatId()`, add `HandleSimTickCompleted(int32)`.

### Fix #2 — AI controller tick order (MODERATE)

**Problem.** `USeinWorldSubsystem::TickAIControllers` snapshot the `AIControllers` array but iterated it in registration order. Registration order depends on actor spawn order + BeginPlay sequencing, which is not deterministic across different clients (GC pass timing, map-load ordering, etc.). Two clients could tick AI controllers in different orders, emitting commands in different orders → different sim state.

**Fix.** Snapshot then `StableSort` by `OwnedPlayerID` before iterating. Player IDs are globally registered via `RegisterPlayer` and are deterministic across clients; sorting by them pins the tick order network-wide.

**File**:
- `Source/SeinARTSCoreEntity/Private/Simulation/SeinWorldSubsystem.cpp::TickAIControllers` — added StableSort with lambda comparing `OwnedPlayerID`.

### Fix #3 — Vision event emission order (MINOR, defense-in-depth)

**Problem.** The vision subsystem diffs `NewVisibleByPlayer` against `LastVisibleByPlayer` and emits `EntityEnteredVision` / `EntityExitedVision` events for transitions. Both the outer `TMap<FSeinPlayerID, TSet<FSeinEntityHandle>>` iteration (hash-based) and inner `TSet<FSeinEntityHandle>` iteration (hash-based) produced events in non-deterministic order.

Vision events are currently render-side only (no sim-state feedback), so this is defense-in-depth — but any future scenario that listens to the visual-event stream and emits sim commands in response would desync. Fixing now costs nothing.

**Fix.** Collect TMap keys into a TArray, sort by `FSeinPlayerID`. Per player, materialize the inner `TSet` as a sorted TArray (sorted by `FSeinEntityHandle::Index` then `Generation`) before iterating. Events now emit in deterministic (PlayerID, EntityIndex, EntityGeneration) order.

**File**:
- `Source/SeinARTSNavigation/Private/Vision/SeinVisionSubsystem.cpp::RunVisionTick` — replaced the double-nested TMap/TSet for-loops with sorted-TArray iteration.

### Build verified

`SeinARTSEditor` Development target builds clean on Win64 after the three fixes — no errors, no warnings.

### Known-acceptable non-determinism (not fixed, not bugs)

Documented here so the next session doesn't chase false positives:

- **`USeinWorldSubsystem::TimeAccumulator` + `FixedDeltaTimeSeconds`** (float) — wall-clock scheduler that decides *when* to run a sim tick. Not sim state. The actual delta passed into `TickSystems` is a deterministic `FFixedPoint`. CLAUDE.md explicitly documents this pitfall.
- **`SeinActorBridge::OnSimTick` — `FMath::Lerp` for render transform** — render-side only, interpolates between sim ticks for smooth visuals. Not sim state.
- **`USeinNavBaker` — wall-clock-tick-driven bake** — bake is an editor-time process, runs on one machine, produces a serialized grid asset. Cross-machine bit-identical bake is an explicit non-goal per DESIGN §13 ("functional determinism via fixed-point quantization; bit-identical requires VCS check-in").
- **`USeinVisionSubsystem::DrawDebugVisualization` + `HandleWorldPostActorTick`** — `FWorldDelegates::OnWorldPostActorTick` wall-clock draw pass for the `SeinARTS.FogOfWar.Debug` console command. Read-only (reads `VisLayer.Visible` / `Explored`, never writes), guarded by `#if ENABLE_DRAW_DEBUG`, cross-world-guarded, and opt-in (defaults off, lazy-binds on enable). Added post-fixes by the linter; audited safe. Not sim state.

---

## 3. Deep dive: framework as it stands

### Module layout (6 modules)

```
SeinARTSCore          Fixed-point math, deterministic primitives, PRNG, SEIN_SIM_SCOPE
SeinARTSCoreEntity    ECS, sim loop, abilities, effects, tags, tech, production,
                      brokers, containment, combat, match flow, diplomacy, voting,
                      replay header, AI controllers, actor bridge, visual events
SeinARTSNavigation    Nav grid + cell data variants, nav volumes + baker + asset,
                      HPA* abstract graph + navlinks, flow-field planner,
                      movement profiles, vision subsystem, terrain volumes,
                      capture points, footprints
SeinARTSEditor        Content Browser factories (Ability/Actor/Effect/Widget),
                      USeinSimComponentFactory + FSeinStructAssetBroker for UDS→AC
                      composition, fixed-point pin factory, thumbnail renderer,
                      class picker, style/icons, nav volume detail panel,
                      SeinDeterministic USTRUCT validator
SeinARTSFramework     Gameplay-layer: player controller (with selection +
                      control groups + smart command dispatch → BrokerOrder),
                      camera pawn, HUD, marquee + drag orders
SeinARTSUIToolkit     View-models, selection model, widget pool, UI BPFL
```

### Cross-cutting invariants (10 locked in DESIGN.md preamble)

| # | Invariant | Status |
|---|---|---|
| 1 | Sim/render separation absolute | ✓ Enforced |
| 2 | Lockstep-compatible (only commands cross wire) | ✓ Enforced |
| 3 | Replay-serializable (command stream + seed = state) | ⚠ Header-only; binary log tail deferred |
| 4 | Tick phases declared (PreTick / CommandProc / AbilityExec / PostTick) | ✓ Enforced |
| 5 | Tag integration for cross-system state | ✓ Enforced |
| 6 | No premature replication abstractions | ✓ Enforced |
| 7 | Designer-editable where possible | ✓ Enforced |
| 8 | Scale-aware grid data (≤5000² cells, compact structs) | ✓ Implemented in nav/vision |
| 9 | Batch-tick heavy systems | ✓ Vision now batches; other batched systems defer by phase |
| 10 | Entity lookup is framework-DNA-level | ✓ `EntityTagIndex` + `NamedEntityRegistry` auto-maintained |

### Primitive checklist

Parallel-agent audits against DESIGN.md returned ✓ MATCHES across all primitives:

- **Entities + Tags** (§1+§3): `FSeinTagData` refcounted, auto-indexed into `EntityTagIndex`, `USeinEntityLookupBPFL` ships, `NamedEntityRegistry` for singletons. `bIsAbstract` for scenario / broker abstract entities.
- **Components (§2)**: `USeinStructComponent` + `FSeinStructAssetBroker` + `USeinSimComponentFactory` handle UDS-as-component. `USeinDynamicComponent` retired. `SeinDeterministic` meta on every framework sim USTRUCT. Validator removes non-deterministic fields from UDS subclasses.
- **Commands + CommandBrokers (§4+§5)**: `FSeinCommand::CommandType` is a gameplay tag. Optional `FInstancedStruct Payload`. Observer-command tree for replay UI reconstruction. `FSeinCommandBrokerData` + `FSeinBrokerMembershipData` + `USeinCommandBrokerResolver` (default resolver ships). Broker ownership invariant enforced.
- **Resources (§6)**: Tag-keyed throughout. `FSeinResourceDefinition` catalog + `USeinFaction::ResourceKit`. `FSeinResourceCost` unified. `USeinResourceBPFL` with CostDirection + ProductionDeductionTiming semantics. Transfer gated by diplomacy.
- **Abilities (§7)**: Full activation flow (cooldown → tag-arbitration → declarative target validation → CanActivate → affordability → deduct → cancel-others → OwnedTags → OnActivate). AutoMoveThen wired via broker. Pathable-target gate consults nav resolver. DefaultCommands moved to `FSeinAbilityData`.
- **Effects (§8)**: `USeinEffect` UObject base. Three scopes (Instance / Archetype / Player). Pending-apply queue prevents intra-tick recursion. GrantedTags refcount-routed via world subsystem. Source-death removal wired.
- **Production (§9)**: Two-bucket cost (AtEnqueue / AtCompletion). Stall-at-completion for pop-cap resources. Progress-proportional refund default + custom override. Entity-or-location rally target, auto-move via broker.
- **Tech (§10)**: Unified with effects — `GrantedTechEffect: TSubclassOf<USeinEffect>`. Refcounted `PlayerTags`. `USeinTechBPFL` availability query.
- **Combat (§11)**: Thin layer — `FSeinCombatData` has only Health + MaxHealth. `SeinApplyDamage` / `SeinApplyHeal` / `SeinApplySplashDamage` + `SeinRollAccuracy` + `SeinMutateAttribute`. Tag-keyed `StatCounters`. `DeathHandler` ability dispatch.
- **Vision (§12)**: **Rebuilt 2026-04-22** to close the MVP gap. `ASeinFogOfWarVolume` (mirrors `ASeinNavVolume`) declares the vision-grid extent; plugin setting `VisionCellSize` governs resolution (default 400 cm, per-volume override). Storage is packed u8 `EVNNNNNN` bitfield + per-layer u8 refcount arrays (one per enabled vision-layer slot, allocated to `VisionCellsX × VisionCellsY`). Plugin setting `VisionLayers[6]` (FName + bEnabled per slot) drives designer-configurable layers; framework ships slot 0 = "Normal" by default. `FSeinVisionData` carries `VisionRange` + `PerceptionLayers` + `EmissionLayers` (FName arrays resolved to slot-bit masks at tick time). Stamp-delta registry keyed by emitter handle + disc template cache (radius-in-cells → cell-offset list). Batched tick on `OnSimTickCompleted` at plugin-settings `VisionTickInterval`. `USeinVisionBPFL` exposes the full query surface. SharedVision aggregation + LOS resolver preserved. Determinism-sorted event emission. See Section 4 for what's still deferred (clipped-raycast LOS, blocker entities, tile-partitioned source registry, 4-bit refcount packing).
- **Terrain (§13)**: `FSeinCellData_Flat/Height/HeightSlope` variants. Per-map tag palette + sparse overflow. Tile partitioning. `ASeinNavVolume` + `USeinNavBaker` (tick-driven) + `USeinNavigationGridAsset`. HPA* abstract graph + navlinks. Flow-field planner with per-cluster Dijkstra + plan cache. `ASeinTerrainVolume` + `FSeinFootprintData` + `FSeinCapturePointData`.
- **Relationships (§14)**: Hybrid primitive — `FSeinContainmentData` base + `FSeinAttachmentSpec` / `FSeinTransportSpec` / `FSeinGarrisonSpec` specializations. Three visibility modes. Death propagation with eject/destroy effect hooks. Deterministic visual-slot assignment.
- **Selection (§15)**: `bSelectable` universal flag. `USeinSelectionBPFL` with full vocabulary. Observer commands logged. Per-tick purge of stale handles from selection + control groups.
- **AI (§16)**: `USeinAIController` abstract UObject base. `USeinAIBPFL`. Registry on world subsystem, ticked at head of CommandProcessing phase — now in deterministic PlayerID order (as of this session).
- **Scenario (§17)**: No new primitive — scenarios are abstract entities with abilities. `USeinScenarioBPFL` for SetSimPaused / PlayPreRenderedCinematic / EndCinematic. Skip mode enum.
- **Match Flow + Voting + Diplomacy + Replay Header (§18)**: Full state machine (Lobby → Starting → Playing → Paused → Ending → Ended). `USeinMatchFlowBPFL`. `FSeinVoteState` + `USeinVoteBPFL`. Directional diplomacy tags with parallel index. 8 framework diplomacy tags. `FSeinReplayHeader` + `USeinReplayBPFL` (header-only persistence).

---

## 4. Gaps and technical debt

### Deferred items from PLAN.md (by urgency)

**Likely-needed-soon** (these may bite the first real game):
1. **Vision §12 residual deferrals** — bulk of the §12 spec landed 2026-04-22 (see Section 3). Still missing:
   - **Clipped-raycast LOS occlusion.** `FSeinVisionData::bRequiresLOS` field exists but the subsystem doesn't consult it. Stamps are always full unobstructed discs.
   - **`FSeinVisionBlockerData` component.** Dynamic blockers (smoke grenades, destructible hedges, ability-spawned cover) unsupported — the struct doesn't exist in the codebase.
   - **Tile-partitioned source registry.** Current impl walks the full entity pool every tick. Fine at a few thousand emitters; bottlenecks above that. DESIGN prescribes iterating only tiles each source's stamp overlaps.
   - **4-bit packed refcounts.** V1 uses u8 (255-cap per cell per layer, way above any realistic density). DESIGN calls for 4-bit nibbles packed 2/byte — saves ~50% of refcount memory. Pure optimization.
   - **u16 bitfield variant (>6 layer cap).** 6 N-bits = 6 designer vision layers. DESIGN flags a u16 variant for games needing 14 layers.
   - **Terrain-level vision blockers.** Terrain-tagged cells blocking vision (forests, fog banks) not wired — would hook into the nav-grid cell-tag palette.
   
   Recommendation: LOS + blocker component is the next meaningful feature slice (enables smoke grenades, line-of-sight-dependent abilities); everything else is optimization.
2. **Per-entrance-border HPA* graph refinement** — MVP is node-per-cluster. Route costs are coarse; paths around cluster borders will look weird at production scale.
3. **Vote eligibility policy** — V1 counts all registered players. Real lobbies need exclude-spectators / exclude-AI / team-only-votes.
4. **Binary replay command-log tail** — header-only for V1; full replay playback needs the binary tick-grouped command log.
5. **`USeinAbility::Requirements` cleanup** — legacy `RequiredTags`/`BlockedTags` fields still on the class, unused by post-Session-2.2 ProcessCommands flow. Either retire or retrofit.

**Probably-fine-deferred** (flagged but lower urgency):
7. Stateful AI hand-off on host migration (V1 restarts from current state)
8. `ESeinResourceOverflowBehavior::RedirectOverflow` (enum reserved, falls through to ClampAtCap)
9. Truce auto-expiration (designer-scripted for V1)
10. Sim-speed multipliers (2x / slow-mo) for skirmish/single-player
11. Per-instance effect config overrides
12. Custom paint tool for terrain authoring
13. u16 bitfield for >6 vision layers (listed in Vision §12 residual deferrals above)
14. Hierarchical cell grid sparse-vision optimization (Variant B)
15. 4-inline tag slots per cell (V1 ships 2; 4 is a one-line bump when profiling demands)
16. Per-class clearance layers for agent radius
17. Multi-layer footprint registration (V1 stamps layer 0 only)
18. Animated navlink traversal (V1 teleports)
19. Spawn-time `CanResearch` BP event gate on `QueueProduction` (availability BPFL reports it but ProcessCommands doesn't enforce it)
20. Starter content BPs across the board (entry/exit abilities, retreat, garrison fire, crew weapon operation, starter AI, starter scenario, vault/drop-down/climb traversal)

### Structural gaps the audits surfaced

- **Zero tests.** PLAN.md's cross-cutting work explicitly says "per phase, write at least one replay-determinism test." Never happened. No `*Test*.cpp` / `*Spec*.cpp` files in `Source/`. This is the single biggest quality gap; see Priority 3 below.
- **Some BPFL naming drift possible.** CLAUDE.md flags this as designer-discipline-enforced. No audit ran over the full naming convention; worth a sweep before release.
- **Designer-facing docs partial.** PLAN.md "mkdocs site updates per phase" wasn't fully delivered. Framework is useable by a developer reading code + DESIGN.md, but not by a designer reading a tutorial.

### Documentation status

- **DESIGN.md** — authoritative, all 17 sections locked.
- **CLAUDE.md** — refreshed after cleanup chapter. Says "18 sections" in one place (stale); actual section count is 17.
- **PLAN.md** — up to date through the cross-cutting integration pass. All Phase 0–5 entries `[COMPLETE]`.
- **HANDOFF.md** — this file (new).

---

## 5. Next-session priorities

The user's explicit priority order for the next major run:

### Priority 1 — Interface / modularization pass

**Goal.** Let third-party developers roll their own fog-of-war, navigation, or other core systems without forking the framework. Selection via plugin settings.

**Scope.**

- **Identify primary modularization candidates.** Top of the list:
  - Vision provider (`USeinVisionSubsystem` today → `ISeinVisionProvider` interface + default impl)
  - Nav provider (`USeinNavigationSubsystem` + grid → `ISeinNavProvider` interface)
  - Pathfinder (`USeinFlowFieldPlanner` → `ISeinPathfinder` interface)
- **Secondary candidates** (review for value before committing):
  - AI controller base class is already subclassable; minor interface wrapping may suffice
  - Actor bridge is render-side, less critical
  - Combat and command brokers are already designer-extensible via subclassing

**Design approach per interface.**
- `ISeinXxx` pure interface (`UINTERFACE(MinimalAPI, Blueprintable)`) with all methods the sim calls.
- Default implementation as a `UWorldSubsystem` (matches current pattern; preserves lifetime).
- Plugin settings entry: `TSoftClassPtr<UObject> XxxProviderClass` with `MetaClass` set to the interface's UClass so the picker filters correctly.
- At sim init, `USeinWorldSubsystem::OnWorldBeginPlay` resolves the selected class, verifies it implements the interface, spawns/binds it.
- Framework code consumes the interface via `IXxx* Provider = USeinWorldSubsystem::GetXxxProvider()` getter that returns the currently-bound instance.

**Required deliverable.**
- Per interface: complete method-by-method API doc covering the sim's contract — what the sim calls, when it calls it, what it expects back, what determinism invariants the implementation must satisfy, what events the implementation must emit.
- DESIGN.md extension: new "§19 Framework extension interfaces" section (or similar) capturing each interface's contract.
- Tooltip text on plugin settings entries that points developers to the design doc.

**Risks.**
- Cross-module dependencies on concrete subsystems will need interface-ification (e.g., `USeinAbility::bRequiresLineOfSight` currently binds to `FSeinLineOfSightResolver` delegate — that already *is* an interface-lite pattern; preserve and document).
- Some subsystems have many friends / tight internal coupling (vision ↔ nav grid). Interface surfaces need to be wide enough for real swap-in.
- Plugin-settings-selected class pattern doesn't load on project open — needs to be resolved at `OnWorldBeginPlay` or sim-start, not class construction.

**Open questions for the next parent to decide.**
- Do we bake a mechanism for "interface provider can be hot-swapped at runtime" or is it lifetime-fixed per match?
- Should providers be UObject-backed (heavy, flexible, UPROPERTY-friendly) or C++-interface-only (light, more coupling-friction)?
- What's the story for interface evolution (adding methods to `ISeinXxx` post-release would break third-party implementations)?

### Priority 2 — Network layer

**Goal.** Multi-client matches. PIE multi-client testing experience matching UE native replication workflow. Everything from Phases 0–5 has been single-machine-only.

**Scope.**

- **Research pass first.** UE's networking infra (actor replication, RPCs, `FNetworkGUID`, lockstep vs snapshot, rollback patterns) is large and specific. First session of this priority should be a "dig into UE source / documentation / shipping RTS examples" read-only pass.
- **Transport layer.** `FSeinCommand` serialization over the wire. Options: piggyback on Unreal's NetSerialize for USTRUCT, or custom binary format. Likely the former.
- **Command stream synchronization.** Lockstep model — each client runs the same sim independently; the host gathers commands, broadcasts a per-tick command set, all clients run tick N with the same commands.
- **Input delay / rollback.** Classic lockstep has input delay (you issue a command, it doesn't fire until tick N+2 so all clients have it). Rollback-style (fighting games) is an alternative; heavier. Start with input-delay lockstep.
- **Desync detection.** State hash already computed per tick (`USeinWorldSubsystem::ComputeStateHash`). Add per-tick hash broadcast + compare + big red on-screen warning on mismatch (see Priority 3).
- **Lobby flow.** UE networking for the pre-match handshake: player connect, faction + team assignment, settings exchange, PRNG seed agreement, match start.
- **PIE multi-client.** UE supports this natively via "Number of Players" in PIE settings. Verify our framework works correctly in that mode. May need project-setting tweaks; may need `AGameMode` subclass refinements.
- **Host migration.** `FSeinMatchSettings::HostDropAction` enum exists (EndMatch / PauseUntilNewHost / AutoMigrate / AITakeover). Wire the actual behavior.

**Required deliverable.**
- Two clients running a networked PIE match, issuing commands, staying deterministically in sync.
- Host disconnection test: one of the four `HostDropAction` modes actually fires.
- Desync detection test: artificially poison one client's state, verify the framework fires the red-alert message.

**Risks.**
- **UE networking is a big beast.** Substantial UE-specific learning curve. Budget at least a full exploratory session before writing code.
- **State-hash false positives.** Hash mismatches can come from float drift in non-sim systems the hash accidentally includes, from TMap iteration order in hash computation, from timing of when ComputeStateHash runs. Need to validate the current hash-compute path is itself deterministic (it iterates ComponentStorages, which is a TMap keyed by UScriptStruct*... that iteration order isn't guaranteed deterministic across binaries).
- **Timing tricky.** Lockstep requires all clients to have a tick's commands before processing that tick. Lag-compensation / stall handling / slow-client behavior needs thought.

**Open questions.**
- Full lockstep vs hybrid (sim-deterministic with occasional snapshot sync)? Lockstep is the clean path and matches the framework's design philosophy.
- How much network code is in `SeinARTSCoreEntity` (sim-core) vs a new `SeinARTSNetwork` module? Recommend a new module so the core stays network-free.
- Do we ship a sample lobby UI or leave entirely to the designer?

### Priority 3 — Determinism testing

**Goal.** High-quality determinism enforcement across compile time, runtime, and observability.

**Three sub-priorities.**

**3a — Compile-time determinism warnings.**
- Already-enforced: `SeinDeterministic` USTRUCT meta on framework structs; the UDS field validator strips non-deterministic types at authoring time.
- **To add**: equivalent BP-variable validator for Blueprint subclasses of `USeinAbility`, `USeinEffect`, `USeinScenario`-style entities. When the designer adds a `float`, `FRandomStream`, `FVector` variable on a sim-layer BP class, warn/reject.
- Hook: UE's `IBlueprintCompilerMessageAction` or `FKismetCompilerContext` callback. May need a custom `UBlueprintEditorUtils` extension.
- Framework PC/BPFL call sites that emit commands could audit for non-sim types in command payloads.

**3b — Runtime desync detection + red-alert UI.**
- State-hash broadcast per-tick (depends on network layer in Priority 2; can prototype in-process with two sim instances side-by-side in PIE first).
- When hash mismatch detected: output log fires `LogSeinDesync: Error` ONCE (don't spam) with tick number + both hashes. Render layer shows a big red on-screen message "SeinARTS: SIMULATION DESYNC DETECTED" with a long duration (9999s or until dismissed).
- Design the message layer carefully: sim-side detects, visual-event emits, render-side renders. The sim can't directly print-to-screen without breaking the boundary.
- Persistent indicator: once desync detected, leave a sticky UI element so testers can't miss it.

**3c — Extensive startup/shutdown + major-process logging.**
- Nav bake already logs well — use as reference pattern.
- Needed for: sim start, sim stop, match start/end, player registration, faction registration, effect system init, vision grid creation, broker creation, AI controller registration.
- Follow existing convention: `LogSeinSim`, `LogSeinNav`, `LogSeinVision` categories.
- Goal: reading the Output Log should give a clear sequential narrative of framework behavior without being spammy (no per-tick logs unless Verbose).
- Specifically: every "big one-shot process" (nav bake, match start, scenario load, replay save) should log beginning, success/failure, and any warnings. Silent failures are not acceptable.

**Required deliverable.**
- A test harness that runs two sim instances on the same command stream in PIE, compares state hashes per tick, and verifies identical.
- A BP-compile warning firing on a test BP that introduces a non-deterministic variable on a sim-layer class.
- A visible red-alert desync display tested by artificially poisoning one sim instance.
- Logged narrative of a full match (start → produce → attack → tech → end) without spam.

**Risks.**
- **State-hash computation itself must be deterministic.** Audit `ComputeStateHash` before trusting it — iterates `ComponentStorages` which is `TMap<UScriptStruct*, ...>`; pointer-keyed TMap iteration order is NOT deterministic.
- **BP compile hooks can be brittle** across UE versions. Prefer hooks that UE officially documents over internal-API tricks.
- **Log volume trade-off.** Too much = spam and hides real signal. Too little = can't diagnose. Log-level discipline matters.

---

## 6. How to start the next session

**Recommended order.**

1. **Read this handoff doc** (you are here) + CLAUDE.md + skim DESIGN.md's `§` of interest.
2. **Confirm the determinism fixes are live.** Build clean, open `Source/SeinARTSNavigation/Public/Vision/SeinVisionSubsystem.h` and verify the big header comment is there and the class is no longer a `FTickableGameObject`.
3. **Pick a priority.** User stated order is 1 (Interface/modularization) → 2 (Network) → 3 (Determinism testing), but they may reprioritize.
4. **For Priority 1**: audit existing subsystems that are candidates for interface extraction. Start with Vision since it's the most clearly-scoped. Design the interface before touching code.
5. **For Priority 2**: spend a session on UE-networking research + feasibility notes. Don't write network code before understanding UE's replication architecture.
6. **For Priority 3**: can be started in parallel with 1 or 2. State-hash audit is a good starting point.

### Before major changes

- Run the determinism audit pattern from this session (parallel Explore agents against `SeinARTSCore`/`CoreEntity`/`Navigation` sim-side modules) periodically. Catches drift.
- Update this handoff doc or spawn a new one per major architectural shift. Avoid letting it go stale.
- Keep DESIGN.md up to date as the source of truth. If the framework's behavior changes, DESIGN.md reflects it before code lands.
- Tests should be introduced BEFORE any new feature work if possible (Priority 3's replay-determinism harness is the highest-leverage test infra).

### Sacred cows

- **Determinism is non-negotiable.** Every decision gets evaluated against "does this preserve bit-identical sim state across clients at each tick?" Float in sim math, wall-clock timers, non-deterministic iteration — all hard-no.
- **Sim/render separation.** Render reads sim state. Sim never reads render state. Visual events cross the boundary, never the reverse.
- **Tags over enums** for designer-extensible vocabularies (command types, effect scopes, damage types, diplomacy states).
- **One primitive per concept.** We resisted adding `USeinScenario` / `USeinTech` / `USeinObjective` primitives during the design pass because existing primitives (entities, abilities, effects) covered the concepts. Future temptation to add "just one more primitive" should be pushed back against.
- **No commercial-game references** in source or docs per the IP-safety policy.

### Anti-patterns to avoid

- Don't add `FTickableGameObject` to sim-side code. Use `USeinWorldSubsystem::OnSimTickCompleted` or register as an `ISeinSystem` with a phase and priority.
- Don't iterate `TSet` / `TMap` in a way that affects sim state without sorting first. Hash order is not your friend.
- Don't use `FMath::Rand*` / `FRandomStream` in sim code. Use `USeinWorldSubsystem::SimRandom` (or spawn your own `FFixedRandom`).
- Don't commit code that changes sim-state-affecting behavior without updating DESIGN.md.
- Don't introduce new module-level dependencies without checking `.Build.cs`. The sim-side modules (`Core`, `CoreEntity`, `Navigation`) should not depend on editor modules or render-side modules.

---

## 7. Quick reference

**Key BPFLs** (the designer-facing API):
- Entity lookup: `USeinEntityLookupBPFL`
- Tags: `USeinTagBPFL`
- Abilities: `USeinAbilityBPFL` (+ availability)
- Effects: `USeinEffectBPFL`
- Resources: `USeinResourceBPFL`
- Production: `USeinProductionBPFL` (+ availability)
- Tech: `USeinTechBPFL` (+ availability)
- Combat: `USeinCombatBPFL`
- Stats: `USeinStatsBPFL`
- Vision: `USeinVisionBPFL`
- Spatial queries: `USeinSpatialBPFL`
- Navigation: `USeinNavigationBPFL`
- Terrain: `USeinTerrainBPFL`
- Brokers: `USeinCommandBrokerBPFL`
- Containment: `USeinContainmentBPFL`
- Selection: `USeinSelectionBPFL`
- Match flow: `USeinMatchFlowBPFL`
- Match settings: `USeinMatchSettingsBPFL`
- Voting: `USeinVoteBPFL`
- Diplomacy: `USeinDiplomacyBPFL`
- Scenario: `USeinScenarioBPFL`
- Replay: `USeinReplayBPFL`
- AI: `USeinAIBPFL`
- Player queries: `USeinPlayerBPFL`
- Simulation mutation (restricted): `USeinSimMutationBPFL`
- Component introspection: `USeinComponentBPFL`
- Math: `UMathBPFL` (fixed-point)
- UI: `USeinUIBPFL`

**Key UObject bases** (designer subclass points):
- `ASeinActor` (entity-backed actor)
- `USeinAbility` (Blueprintable, authored in BP)
- `USeinEffect` (Blueprintable, authored in BP)
- `USeinAIController` (Blueprintable, authored in BP)
- `USeinCommandBrokerResolver` (Blueprintable)
- `USeinMovementProfile` (Blueprintable)
- `ASeinNavVolume`, `ASeinTerrainVolume`, `ASeinNavLinkProxy` (editor-placed authoring actors)
- `USeinNavigationGridAsset`, `USeinMatchSettingsPreset` (data asset)

**Key structs** (sim-state payloads):
- Entities: `FSeinEntityHandle`, `FSeinPlayerID`, `FSeinFactionID`
- Components: `FSeinTagData`, `FSeinCombatData`, `FSeinMovementData`, `FSeinAbilityData`, `FSeinActiveEffectsData`, `FSeinProductionData`, `FSeinSquadData`, `FSeinSquadMemberData`, `FSeinResourceIncomeData`, `FSeinVisionData`, `FSeinVisionBlockerData`, `FSeinFootprintData`, `FSeinCapturePointData`, `FSeinCommandBrokerData`, `FSeinBrokerMembershipData`, `FSeinContainmentData`, `FSeinContainmentMemberData`, `FSeinAttachmentSpec`, `FSeinTransportSpec`, `FSeinGarrisonSpec`, `FSeinLifespanData`
- Commands + events: `FSeinCommand`, `FSeinVisualEvent`
- Match state: `FSeinMatchSettings`, `FSeinVoteState`, `FSeinDiplomacyKey`, `FSeinReplayHeader`, `FSeinPlayerRegistration`, `FSeinPlayerState`

**Key subsystems**:
- `USeinWorldSubsystem` — sim core
- `USeinActorBridgeSubsystem` — render bridge
- `USeinNavigationSubsystem` — nav grid host
- `USeinVisionSubsystem` — vision (sim-tick-driven as of 2026-04-22)
- `USeinUISubsystem` — UI refresh broadcast

---

End of handoff.
