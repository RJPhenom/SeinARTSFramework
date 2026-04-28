/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWorldSubsystem.cpp
 * @brief   Implementation of the core simulation subsystem.
 */

#include "Simulation/SeinWorldSubsystem.h"
#include "Actor/SeinActor.h"
#include "AI/SeinAIController.h"
#include "HAL/IConsoleManager.h"
#include "Data/SeinArchetypeDefinition.h"
#include "Data/SeinFaction.h"
#include "Settings/PluginSettings.h"
#include "Core/SeinSimContext.h"
#include "Abilities/SeinAbility.h"
#include "Abilities/SeinAbilityValidation.h"
#include "Abilities/SeinLatentActionManager.h"
#include "Components/SeinAbilityData.h"
#include "Components/SeinActiveEffectsData.h"
#include "Components/SeinAttachmentSpec.h"
#include "Components/SeinBrokerMembershipData.h"
#include "Components/SeinCommandBrokerData.h"
#include "Components/SeinContainmentData.h"
#include "Components/SeinContainmentMemberData.h"
#include "Components/SeinProductionData.h"
#include "Components/SeinTagData.h"
#include "Components/SeinTransportSpec.h"
#include "Components/ActorComponents/SeinActorComponent.h"
#include "Brokers/SeinCommandBrokerResolver.h"
#include "Brokers/SeinDefaultCommandBrokerResolver.h"
#include "Attributes/SeinModifier.h"
#include "Attributes/SeinAttributeResolver.h"
#include "Core/SeinSimContext.h"
#include "Effects/SeinEffect.h"
#include "Lib/SeinResourceBPFL.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "Containers/Ticker.h"
#include "StructUtils/InstancedStruct.h"

// Built-in systems
#include "Simulation/Systems/SeinEffectTickSystem.h"
#include "Simulation/Systems/SeinCooldownSystem.h"
#include "Simulation/Systems/SeinAbilityTickSystem.h"
#include "Simulation/Systems/SeinCommandBrokerSystem.h"
#include "Simulation/Systems/SeinProductionSystem.h"
#include "Simulation/Systems/SeinResourceSystem.h"
#include "Simulation/Systems/SeinPenetrationResolutionSystem.h"
#include "Simulation/Systems/SeinSpatialHashSystem.h"
#include "Simulation/Systems/SeinStateHashSystem.h"
#include "Simulation/Systems/SeinLifespanSystem.h"

#include "Brokers/SeinBrokerTypes.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinSim, Log, All);

void USeinWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	EntityPool.Initialize(1024);
	CurrentTick = 0;
	TimeAccumulator = 0.0f;
	bIsRunning = false;

	// Create latent action manager
	LatentActionManager = NewObject<USeinLatentActionManager>(this);

	// Read settings
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	FixedDeltaTimeSeconds = 1.0f / static_cast<float>(Settings->SimulationTickRate);

	// Spatial hash — cell size 200 cm balances bucket fan-out cost against
	// query precision. Origin = world (0,0,0); levels offset from origin
	// just produce sparse buckets at high indices, no correctness impact.
	SpatialHash.Initialize(FFixedPoint::FromInt(200), FFixedVector::ZeroVector);

	// Register built-in systems
	BuiltInSystems.Add(new FSeinEffectTickSystem());
	BuiltInSystems.Add(new FSeinSpatialHashSystem());
	BuiltInSystems.Add(new FSeinCooldownSystem());
	BuiltInSystems.Add(new FSeinAbilityTickSystem());
	BuiltInSystems.Add(new FSeinProductionSystem());
	BuiltInSystems.Add(new FSeinResourceSystem());
	BuiltInSystems.Add(new FSeinLifespanSystem());
	BuiltInSystems.Add(new FSeinCommandBrokerSystem());
	BuiltInSystems.Add(new FSeinPenetrationResolutionSystem());
	BuiltInSystems.Add(new FSeinStateHashSystem());

	for (ISeinSystem* Sys : BuiltInSystems)
	{
		RegisterSystem(Sys);
	}

	// Auto-register the Neutral player (ID 0). Entities that logically have "no
	// player" (neutral capture points, resource piles, scenario owners, environmental
	// hazards) resolve to this sentinel. See §1 Entities, "Ownership" decision.
	RegisterPlayer(FSeinPlayerID::Neutral(), FSeinFactionID(), /*TeamID=*/0);

	UE_LOG(LogSeinSim, Log, TEXT("SeinWorldSubsystem initialized (tick rate: %d Hz, %d systems)"),
		Settings->SimulationTickRate, Systems.Num());
}

void USeinWorldSubsystem::Deinitialize()
{
	StopSimulation();

	// Clean up component storages
	for (auto& Pair : ComponentStorages)
	{
		delete Pair.Value;
	}
	ComponentStorages.Empty();

	// Clean up built-in systems
	for (ISeinSystem* Sys : BuiltInSystems)
	{
		delete Sys;
	}
	BuiltInSystems.Empty();

	EntityPool.Reset();
	PlayerStates.Empty();
	Systems.Empty();
	PendingCommands.Clear();
	PendingDestroy.Empty();
	EntityTagIndex.Empty();
	NamedEntityRegistry.Empty();

	Super::Deinitialize();

	UE_LOG(LogSeinSim, Log, TEXT("SeinWorldSubsystem deinitialized"));
}

void USeinWorldSubsystem::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);

	USeinWorldSubsystem* Self = CastChecked<USeinWorldSubsystem>(InThis);
	for (auto& Pair : Self->ComponentStorages)
	{
		if (ISeinComponentStorage* Storage = Pair.Value)
		{
			Storage->CollectReferences(Collector, Self);
		}
	}
}

// ==================== Simulation Control ====================

void USeinWorldSubsystem::StartSimulation()
{
	if (bIsRunning)
	{
		UE_LOG(LogSeinSim, Warning, TEXT("Simulation already running"));
		return;
	}

	bIsRunning = true;
	TimeAccumulator = 0.0f;

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &USeinWorldSubsystem::TickSimulation)
	);

	UE_LOG(LogSeinSim, Log, TEXT("Simulation started"));
}

void USeinWorldSubsystem::StopSimulation()
{
	if (!bIsRunning) return;

	bIsRunning = false;
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	UE_LOG(LogSeinSim, Log, TEXT("Simulation stopped at tick %d"), CurrentTick);
}

float USeinWorldSubsystem::GetInterpolationAlpha() const
{
	if (FixedDeltaTimeSeconds > 0.0f)
	{
		return FMath::Clamp(TimeAccumulator / FixedDeltaTimeSeconds, 0.0f, 1.0f);
	}
	return 0.0f;
}

bool USeinWorldSubsystem::TickSimulation(float DeltaTime)
{
	if (!bIsRunning) return false;

	// Paused sim: freeze the wall-clock accumulator (no drift-to-resume catch-up
	// spikes) and skip the tick-system loop entirely. Commands accumulate in
	// PendingCommands (Tactical pause per DESIGN §17 / §18) and flush on resume.
	// Visual events already buffered flush via actor bridge's own read loop.
	if (bSimPaused)
	{
		TimeAccumulator = 0.0f;
		return true;
	}

	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	const int32 MaxTicks = Settings->MaxTicksPerFrame;

	// TicksPerTurn: how many sim ticks make up one network turn. Derived from
	// the two settings; integer division so a misconfiguration doesn't yield
	// a fractional gate (better to round down and re-check sooner). Only
	// consulted when the lockstep resolver is bound (USeinNetSubsystem
	// registers it once the local slot is assigned).
	const int32 TicksPerTurn = (Settings->TurnRate > 0)
		? FMath::Max(1, Settings->SimulationTickRate / Settings->TurnRate)
		: 1;

	TimeAccumulator += DeltaTime;

	int32 TicksProcessed = 0;
	while (TimeAccumulator >= FixedDeltaTimeSeconds && TicksProcessed < MaxTicks)
	{
		const int32 NextTick = CurrentTick + 1;

		// Lockstep gate (Phase 2b). At each turn boundary, ask the network
		// layer whether the assembled turn for the upcoming turn is ready.
		// If not, stall — leave the accumulator alone so we retry next frame.
		// Resolver unbound (Standalone, networking disabled, or NetSubsystem
		// hasn't latched yet) = no gating, sim runs free.
		if (TurnReadyResolver.IsBound() && (NextTick % TicksPerTurn == 0))
		{
			const int32 NextTurn = NextTick / TicksPerTurn;
			if (!TurnReadyResolver.Execute(NextTurn))
			{
				// Stall — break out of the catch-up loop without consuming
				// the accumulator. Next frame's pump will retry. The "falling
				// behind" warning at the bottom is suppressed by the early
				// break since TicksProcessed < MaxTicks may still be true.
				break;
			}
			if (TurnConsumeNotifier.IsBound())
			{
				TurnConsumeNotifier.Execute(NextTurn);
			}
		}

		CurrentTick = NextTick;
		TimeAccumulator -= FixedDeltaTimeSeconds;
		TicksProcessed++;

		// Convert to deterministic fixed-point (from compile-time constant, not runtime float)
		FFixedPoint SimDeltaTime = FFixedPoint::One / FFixedPoint::FromInt(Settings->SimulationTickRate);

		TickSystems(SimDeltaTime);

#if !UE_BUILD_SHIPPING
		// Determinism verification: when the Log cvar is on, dump the sim
		// state hash each tick. Run two PIE clients (or two PIE sessions)
		// with this enabled and diff the logs — any divergence pinpoints
		// the tick where lockstep breaks. Gated off in shipping builds so
		// the hash walk doesn't cost production CPU.
		//
		// Two log levels:
		//   = 1: hash only — `StateHash[tick N] = 0xXXXX`. Compact, finds
		//        first divergent tick. Use for long sessions.
		//   = 2: hash + per-entity dump on tick 1, then hash-only on
		//        subsequent ticks. Tick 1 is the initial state — diffing
		//        two log files at tick 1 reveals what's structurally
		//        different about the starting world (entity IDs / owners
		//        / positions). Best for "spawns are wrong sometimes" hunts.
		//   = 3: hash + per-entity dump EVERY tick. Verbose; use only for
		//        narrow ranges or very early divergences.
		{
			static IConsoleVariable* CVarLog = IConsoleManager::Get().FindConsoleVariable(TEXT("Sein.Sim.StateHash.Log"));
			const int32 StateLogLevel = CVarLog ? CVarLog->GetInt() : 0;
			if (StateLogLevel != 0)
			{
				UE_LOG(LogSeinSim, Log, TEXT("StateHash[tick %d] = 0x%08x"),
					CurrentTick, static_cast<uint32>(ComputeStateHash()));

				const bool bDumpEntities = (StateLogLevel >= 3) || (StateLogLevel == 2 && CurrentTick == 1);
				if (bDumpEntities)
				{
					UE_LOG(LogSeinSim, Log, TEXT("StateHash[tick %d] entity dump  (active=%d):"),
						CurrentTick, EntityPool.GetActiveCount());

					// Walk every alive entity and print ID, owner, and the
					// raw fixed-point position. Raw int64 values diff
					// cleanly across log files (no float-to-string drift).
					EntityPool.ForEachEntity([this](FSeinEntityHandle Handle, const FSeinEntity& Entity)
					{
						const FSeinPlayerID Owner = EntityPool.GetOwner(Handle);
						const FFixedVector& L = Entity.Transform.Location;
						UE_LOG(LogSeinSim, Log,
							TEXT("  H(%d:%d)  slot=%u  pos=(%lld, %lld, %lld) [raw 32.32]"),
							Handle.Index, Handle.Generation, Owner.Value,
							L.X.Value, L.Y.Value, L.Z.Value);
					});
				}
			}
		}
#endif

		OnSimTickCompleted.Broadcast(CurrentTick);
	}

	if (TicksProcessed >= MaxTicks && TimeAccumulator > FixedDeltaTimeSeconds)
	{
		UE_LOG(LogSeinSim, Warning, TEXT("Simulation falling behind! Clamping accumulator."));
		TimeAccumulator = FixedDeltaTimeSeconds;
	}

	return true;
}

void USeinWorldSubsystem::TickSystems(FFixedPoint DeltaTime)
{
	SEIN_SIM_SCOPE

	SortSystemsIfNeeded();

	// Phase 1: PreTick — effects, cooldowns, resources
	for (ISeinSystem* System : Systems)
	{
		if (System->GetPhase() == ESeinTickPhase::PreTick)
		{
			System->Tick(DeltaTime, *this);
		}
	}

	// Advance the match state machine (DESIGN §18) — Starting-state pre-match
	// countdown transitions to Playing once the deadline tick is reached.
	TickMatchState();
	// Expire idle votes (DESIGN §18 voting primitive).
	TickVotes();

	// Phase 2: CommandProcessing — tick AI first (DESIGN §16) so emitted
	// commands accumulate in PendingCommands and get drained same-tick.
	TickAIControllers(DeltaTime);
	ProcessCommands();
	for (ISeinSystem* System : Systems)
	{
		if (System->GetPhase() == ESeinTickPhase::CommandProcessing)
		{
			System->Tick(DeltaTime, *this);
		}
	}

	// Phase 3: AbilityExecution — tick active abilities and latent actions
	if (LatentActionManager)
	{
		LatentActionManager->TickAll(DeltaTime, *this);
	}
	for (ISeinSystem* System : Systems)
	{
		if (System->GetPhase() == ESeinTickPhase::AbilityExecution)
		{
			System->Tick(DeltaTime, *this);
		}
	}

	// Phase 4: PostTick — cleanup, state hash
	ProcessDeferredDestroys();
	for (ISeinSystem* System : Systems)
	{
		if (System->GetPhase() == ESeinTickPhase::PostTick)
		{
			System->Tick(DeltaTime, *this);
		}
	}
}

// ==================== Command Processing ====================

void USeinWorldSubsystem::ProcessCommands()
{
	// Broadcast for debug tooling before processing (commands are still in the buffer)
	if (PendingCommands.Num() > 0)
	{
		OnCommandsProcessing.Broadcast(CurrentTick, PendingCommands.GetCommands());
	}

	for (const FSeinCommand& Cmd : PendingCommands.GetCommands())
	{
		// Observer commands live under SeinARTS.Command.Type.Observer.* — logged
		// for replay but never processed by the sim.
		if (Cmd.IsObserverCommand())
		{
			continue;
		}

		// Convenience: fire a CommandRejected visual event with the original
		// command's type tag + a reason tag under SeinARTS.Command.Reject.* so UI
		// can distinguish "not enough resources" from "can't reach there" etc.
		auto RejectCommand = [this, &Cmd](FGameplayTag Reason = FGameplayTag())
		{
			EnqueueVisualEvent(FSeinVisualEvent::MakeCommandRejectedEvent(
				Cmd.PlayerID, Cmd.EntityHandle, Cmd.CommandType, Reason));
		};

		// Match flow commands (DESIGN §18). Handled before the spectator / pause
		// / starting-state filters so Resume / End / Restart can always unstick
		// the sim. No entity handle required — these target the subsystem itself.
		if (Cmd.CommandType == SeinARTSTags::Command_Type_StartMatch)
		{
			FSeinMatchSettings Settings;
			if (Cmd.Payload.IsValid() && Cmd.Payload.GetScriptStruct() == FSeinMatchSettings::StaticStruct())
			{
				Settings = Cmd.Payload.Get<FSeinMatchSettings>();
			}
			StartMatch(Settings);
			continue;
		}
		if (Cmd.CommandType == SeinARTSTags::Command_Type_EndMatch)
		{
			// Winner = PlayerID on the command; reason = AbilityTag slot.
			EndMatch(Cmd.PlayerID, Cmd.AbilityTag);
			continue;
		}
		if (Cmd.CommandType == SeinARTSTags::Command_Type_PauseMatchRequest)
		{
			SetSimPaused(true, CurrentMatchSettings.DefaultPauseMode);
			continue;
		}
		if (Cmd.CommandType == SeinARTSTags::Command_Type_ResumeMatchRequest)
		{
			SetSimPaused(false);
			continue;
		}
		if (Cmd.CommandType == SeinARTSTags::Command_Type_ConcedeMatch)
		{
			// V1: concede immediately ends the match with no-winner. Designers
			// who want per-team victory / last-player-standing wire their own
			// scenario + call EndMatch with the right winner.
			EndMatch(FSeinPlayerID::Neutral(), SeinARTSTags::Command_Type_ConcedeMatch);
			continue;
		}
		if (Cmd.CommandType == SeinARTSTags::Command_Type_RestartMatch)
		{
			MatchState = ESeinMatchState::Lobby;
			EnqueueVisualEvent(FSeinVisualEvent::MakeMatchFlowEvent(ESeinVisualEventType::MatchEnded));
			continue;
		}

		// Vote commands (DESIGN §18 voting primitive).
		if (Cmd.CommandType == SeinARTSTags::Command_Type_StartVote)
		{
			FSeinStartVoteCommandPayload Pay;
			if (Cmd.Payload.IsValid() && Cmd.Payload.GetScriptStruct() == FSeinStartVoteCommandPayload::StaticStruct())
			{
				Pay = Cmd.Payload.Get<FSeinStartVoteCommandPayload>();
			}
			StartVote(Cmd.AbilityTag, Pay.Resolution, Pay.RequiredThreshold, Pay.ExpiresInTicks, Cmd.PlayerID);
			continue;
		}
		if (Cmd.CommandType == SeinARTSTags::Command_Type_CastVote)
		{
			CastVote(Cmd.AbilityTag, Cmd.PlayerID, Cmd.QueueIndex);
			continue;
		}

		// Diplomacy command (DESIGN §18). Gated on `bAllowMidMatchDiplomacy`.
		if (Cmd.CommandType == SeinARTSTags::Command_Type_ModifyDiplomacy)
		{
			// Allow while in Lobby/Starting regardless of the gate — diplomacy
			// is effectively set during match-setup. Only gate while Playing+.
			const bool bMidMatch = (MatchState == ESeinMatchState::Playing ||
				MatchState == ESeinMatchState::Paused);
			if (bMidMatch && !CurrentMatchSettings.bAllowMidMatchDiplomacy)
			{
				EnqueueVisualEvent(FSeinVisualEvent::MakeCommandRejectedEvent(
					Cmd.PlayerID, Cmd.EntityHandle, Cmd.CommandType,
					SeinARTSTags::Command_Reject_DiplomacyLocked));
				continue;
			}
			if (Cmd.Payload.IsValid() && Cmd.Payload.GetScriptStruct() == FSeinDiplomacyCommandPayload::StaticStruct())
			{
				const FSeinDiplomacyCommandPayload& P = Cmd.Payload.Get<FSeinDiplomacyCommandPayload>();
				ApplyDiplomacyDelta(P.FromPlayer, P.ToPlayer, P.TagsToAdd, P.TagsToRemove);
			}
			continue;
		}

		// Global filters on sim-mutating commands (DESIGN §18).
		auto EmitFilterReject = [this, &Cmd](FGameplayTag Reason)
		{
			EnqueueVisualEvent(FSeinVisualEvent::MakeCommandRejectedEvent(
				Cmd.PlayerID, Cmd.EntityHandle, Cmd.CommandType, Reason));
		};
		if (const FSeinPlayerState* PS = GetPlayerState(Cmd.PlayerID))
		{
			if (PS->bIsSpectator)
			{
				EmitFilterReject(SeinARTSTags::Command_Reject_SpectatorForbidden);
				continue;
			}
		}
		if (bSimPausedHard)
		{
			EmitFilterReject(SeinARTSTags::Command_Reject_SimPaused);
			continue;
		}
		if (MatchState == ESeinMatchState::Starting)
		{
			EmitFilterReject(SeinARTSTags::Command_Reject_MatchStateInvalid);
			continue;
		}

		// Ping commands don't require a valid entity — emit a visual event and continue.
		if (Cmd.CommandType == SeinARTSTags::Command_Type_Ping)
		{
			EnqueueVisualEvent(FSeinVisualEvent::MakePingEvent(
				Cmd.PlayerID, Cmd.TargetLocation, Cmd.TargetEntity));
			continue;
		}

		// BrokerOrder targets a member list (Cmd.EntityList), not Cmd.EntityHandle.
		// Handle it before the single-entity validity guard below.
		if (Cmd.CommandType == SeinARTSTags::Command_Type_BrokerOrder)
		{
			if (Cmd.EntityList.Num() == 0)
			{
				RejectCommand(SeinARTSTags::Command_Reject_InvalidTarget);
				continue;
			}

			// Filter members by ownership (DESIGN §5 "wholly-single-player" invariant).
			// When match settings enable `bAlliedCommandSharing`, the filter widens to
			// accept members owned by allies that grant `Permission.CommandSharing`
			// toward the issuing player (DESIGN §18 cross-cutting hook). The
			// broker's owner stays `Cmd.PlayerID` — allied members join but the
			// broker is still single-owner for all downstream semantics.
			const bool bAlliedSharingOn = CurrentMatchSettings.bAlliedCommandSharing;
			TArray<FSeinEntityHandle> Filtered;
			Filtered.Reserve(Cmd.EntityList.Num());
			for (const FSeinEntityHandle& M : Cmd.EntityList)
			{
				if (!EntityPool.IsValid(M)) continue;
				const FSeinPlayerID MemberOwner = GetEntityOwner(M);
				if (MemberOwner == Cmd.PlayerID) { Filtered.Add(M); continue; }
				if (bAlliedSharingOn)
				{
					const bool bAllied = GetDiplomacyTags(MemberOwner, Cmd.PlayerID)
						.HasTagExact(SeinARTSTags::Diplomacy_Permission_Allied)
						&& GetDiplomacyTags(Cmd.PlayerID, MemberOwner)
							.HasTagExact(SeinARTSTags::Diplomacy_Permission_Allied);
					const bool bCmdShared = GetDiplomacyTags(MemberOwner, Cmd.PlayerID)
						.HasTagExact(SeinARTSTags::Diplomacy_Permission_CommandSharing);
					if (bAllied && bCmdShared) { Filtered.Add(M); continue; }
				}
				// else: foreign member, drop silently (caller-side UX should have filtered).
			}
			if (Filtered.Num() == 0)
			{
				RejectCommand(SeinARTSTags::Command_Reject_InvalidTarget);
				continue;
			}

			// Extract the typed BrokerOrder payload — smart-resolution context +
			// drag-order endpoint. Missing payload is a malformed command.
			const FSeinBrokerOrderPayload* Payload = Cmd.Payload.GetPtr<FSeinBrokerOrderPayload>();
			if (!Payload)
			{
				RejectCommand(SeinARTSTags::Command_Reject_InvalidTarget);
				continue;
			}

			FSeinBrokerQueuedOrder Order;
			Order.Context = Payload->CommandContext;
			Order.TargetEntity = Cmd.TargetEntity;
			Order.TargetLocation = Cmd.TargetLocation;
			Order.FormationEnd = Payload->FormationEnd;

			// If shift-queued and every member already shares a broker, append to
			// that broker's queue. If the shift-click targets a strict subset of
			// the broker's live member list, mark the appended order as
			// subset-targeted so only those members dispatch for it — non-target
			// members keep doing whatever they were doing (DESIGN §5 queue
			// semantics, shift-click-on-subset UX).
			FSeinEntityHandle ExistingBroker;
			if (Cmd.bQueueCommand)
			{
				ExistingBroker = FindSharedBroker(Filtered);
			}
			if (ExistingBroker.IsValid())
			{
				if (FSeinCommandBrokerData* Broker = GetComponent<FSeinCommandBrokerData>(ExistingBroker))
				{
					// Strict subset? Order is TargetMembers-scoped. Full match? All-members.
					if (Filtered.Num() < Broker->Members.Num())
					{
						Order.TargetMembers = Filtered;
					}
					Broker->OrderQueue.Add(Order);
				}
			}
			else
			{
				// Non-shared (or non-queued) — create/reuse broker for the filtered
				// set. Order applies to all members of the new broker by default.
				CreateBrokerForMembers(Filtered, Cmd.PlayerID, Order);
			}
			continue;
		}

		if (!EntityPool.IsValid(Cmd.EntityHandle))
		{
			RejectCommand(SeinARTSTags::Command_Reject_MissingComponent);
			continue;
		}

		if (Cmd.CommandType == SeinARTSTags::Command_Type_ActivateAbility)
		{
			FSeinAbilityData* AbilityComp = GetComponent<FSeinAbilityData>(Cmd.EntityHandle);
			if (!AbilityComp)
			{
				UE_LOG(LogSeinSim, Warning, TEXT("ActivateAbility[%s]: entity %s has no FSeinAbilityData"),
					*Cmd.AbilityTag.ToString(), *Cmd.EntityHandle.ToString());
				RejectCommand(SeinARTSTags::Command_Reject_MissingComponent);
				continue;
			}

			USeinAbility* Ability = AbilityComp->FindAbilityByTag(Cmd.AbilityTag);
			if (!Ability)
			{
				UE_LOG(LogSeinSim, Warning, TEXT("ActivateAbility[%s]: entity %s has no ability with that tag (%d instances from %d granted classes)"),
					*Cmd.AbilityTag.ToString(), *Cmd.EntityHandle.ToString(),
					AbilityComp->AbilityInstances.Num(), AbilityComp->GrantedAbilityClasses.Num());
				RejectCommand(SeinARTSTags::Command_Reject_InvalidTarget);
				continue;
			}
			// Full activation ordering per DESIGN §7:
			//   1. Cooldown check
			//   2. ActivationBlockedTags vs entity tags
			//   3. Declarative target validation (range / tags / LOS)
			//   4. CanActivate BP escape hatch
			//   5. Affordability check
			//   6. Deduct cost
			//   7. Cancel-others via CancelAbilitiesWithTag
			//   8. Record deducted cost snapshot + USeinAbility::ActivateAbility
			//      (which handles cooldown start + OwnedTags grant + OnActivate)

			// 1. Cooldown
			if (Ability->IsOnCooldown())
			{
				UE_LOG(LogSeinSim, Warning, TEXT("ActivateAbility[%s]: on cooldown"), *Cmd.AbilityTag.ToString());
				RejectCommand(SeinARTSTags::Command_Reject_OnCooldown);
				continue;
			}

			// 2. ActivationBlockedTags
			if (!Ability->ActivationBlockedTags.IsEmpty())
			{
				const FSeinTagData* TagComp = GetComponent<FSeinTagData>(Cmd.EntityHandle);
				if (TagComp && TagComp->CombinedTags.HasAny(Ability->ActivationBlockedTags))
				{
					UE_LOG(LogSeinSim, Warning, TEXT("ActivateAbility[%s]: blocked by entity tags"),
						*Cmd.AbilityTag.ToString());
					RejectCommand(SeinARTSTags::Command_Reject_BlockedByTag);
					continue;
				}
			}

			// 3. Declarative target validation (range / tags / LOS)
			const ESeinAbilityTargetValidationResult ValidationResult = FSeinAbilityValidation::ValidateTarget(
				*Ability, Cmd.EntityHandle, Cmd.TargetEntity, Cmd.TargetLocation, *this);
			if (ValidationResult != ESeinAbilityTargetValidationResult::Valid)
			{
				// OutOfRange + AutoMoveThen: prepend an internal Move order on a
				// single-member broker, then queue the original ability behind it.
				// The Move targets the target's current position (or Cmd.TargetLocation
				// if no target entity). Cost is deducted upfront — the player's click
				// commits at AutoMoveThen-acceptance time (classic RTS UX). Broker
				// dispatch skips cost re-deduction per SeinCommandBrokerSystem.
				if (ValidationResult == ESeinAbilityTargetValidationResult::OutOfRange &&
					Ability->OutOfRangeBehavior == ESeinOutOfRangeBehavior::AutoMoveThen)
				{
					// Member must have a Move ability to fulfill the prefix. If not,
					// there's nothing to auto-move with — reject as OutOfRange.
					if (!AbilityComp->HasAbilityWithTag(SeinARTSTags::Ability_Move))
					{
						UE_LOG(LogSeinSim, Warning,
							TEXT("ActivateAbility[%s]: AutoMoveThen requested but entity has no Move ability; rejecting"),
							*Cmd.AbilityTag.ToString());
						RejectCommand(SeinARTSTags::Command_Reject_OutOfRange);
						continue;
					}

					// Affordability check stays — AutoMoveThen is an "accept this
					// command" path, not a free pass on cost gates.
					if (!USeinResourceBPFL::SeinCanAfford(this, Cmd.PlayerID, Ability->ResourceCost))
					{
						RejectCommand(SeinARTSTags::Command_Reject_Unaffordable);
						continue;
					}
					USeinResourceBPFL::SeinDeduct(this, Cmd.PlayerID, Ability->ResourceCost);

					// Resolve the Move destination. If the command targets an entity,
					// use its current world position; else use the raw TargetLocation.
					FFixedVector MoveDest = Cmd.TargetLocation;
					if (Cmd.TargetEntity.IsValid())
					{
						if (const FSeinEntity* Tgt = GetEntity(Cmd.TargetEntity))
						{
							MoveDest = Tgt->Transform.GetLocation();
						}
					}

					const TArray<FSeinEntityHandle> SingleMember = { Cmd.EntityHandle };

					// Move-prefix + follow-up targeted at just this member. Both
					// orders carry the one-broker-per-member invariant and the
					// subset-targeting machinery so non-target members (if this
					// folds into an existing multi-member broker) stay untouched.
					FSeinBrokerQueuedOrder MovePrefix;
					MovePrefix.Context.AddTag(SeinARTSTags::Ability_Move);
					MovePrefix.TargetLocation = MoveDest;
					MovePrefix.TargetMembers = SingleMember;
					MovePrefix.bIsInternalPrefix = true;

					FSeinBrokerQueuedOrder Followup;
					Followup.Context.AddTag(Cmd.AbilityTag);
					Followup.TargetEntity = Cmd.TargetEntity;
					Followup.TargetLocation = Cmd.TargetLocation;
					Followup.TargetMembers = SingleMember;
					Followup.bIsInternalPrefix = true;

					// Prefer the member's existing broker if it has one — inject the
					// [Move, Follow-up] pair right after the currently-executing
					// order. One-broker-per-member preserved, shift-queue on the
					// existing broker preserved, non-target members unaffected.
					FSeinEntityHandle ExistingBroker;
					if (const FSeinBrokerMembershipData* Memb = GetComponent<FSeinBrokerMembershipData>(Cmd.EntityHandle))
					{
						ExistingBroker = Memb->CurrentBrokerHandle;
					}
					if (ExistingBroker.IsValid() && EntityPool.IsValid(ExistingBroker))
					{
						if (FSeinCommandBrokerData* Broker = GetComponent<FSeinCommandBrokerData>(ExistingBroker))
						{
							// Insert position: right after current order if executing
							// (index 1), else at front (index 0) so it runs next tick.
							const int32 InsertAt = Broker->bIsExecuting ? 1 : 0;
							Broker->OrderQueue.Insert(Followup, FMath::Min(InsertAt, Broker->OrderQueue.Num()));
							Broker->OrderQueue.Insert(MovePrefix, FMath::Min(InsertAt, Broker->OrderQueue.Num()));
							continue;
						}
					}

					// No existing broker — spawn one for this single member with the
					// Move+Follow-up queued. CreateBrokerForMembers takes a first
					// order and pre-queues it; append follow-up after.
					FSeinEntityHandle BrokerHandle = CreateBrokerForMembers(SingleMember, Cmd.PlayerID, MovePrefix);
					if (BrokerHandle.IsValid())
					{
						if (FSeinCommandBrokerData* Broker = GetComponent<FSeinCommandBrokerData>(BrokerHandle))
						{
							Broker->OrderQueue.Add(Followup);
						}
					}
					continue;
				}
				UE_LOG(LogSeinSim, Warning, TEXT("ActivateAbility[%s]: target validation failed (%d)"),
					*Cmd.AbilityTag.ToString(), static_cast<int32>(ValidationResult));
				FGameplayTag ReasonTag;
				switch (ValidationResult)
				{
				case ESeinAbilityTargetValidationResult::OutOfRange:     ReasonTag = SeinARTSTags::Command_Reject_OutOfRange; break;
				case ESeinAbilityTargetValidationResult::NoLineOfSight:  ReasonTag = SeinARTSTags::Command_Reject_NoLineOfSight; break;
				default:                                                  ReasonTag = SeinARTSTags::Command_Reject_InvalidTarget; break;
				}
				RejectCommand(ReasonTag);
				continue;
			}

			// 3b. Pathable-target gate — if enabled on this ability, consult the
			// nav-registered resolver for reachability. From/To stay FFixedVector
			// end-to-end; no lossy float round-trip on the sim path.
			if (Ability->bRequiresPathableTarget && PathableTargetResolver.IsBound())
			{
				const FSeinEntity* ActingEntity = GetEntity(Cmd.EntityHandle);
				if (ActingEntity)
				{
					const FFixedVector FromWorld = ActingEntity->Transform.GetLocation();
					const FFixedVector ToWorld = Cmd.TargetLocation;

					FGameplayTagContainer AgentTags;
					if (const FSeinTagData* TagComp = GetComponent<FSeinTagData>(Cmd.EntityHandle))
					{
						AgentTags = TagComp->CombinedTags;
					}

					if (!PathableTargetResolver.Execute(FromWorld, ToWorld, AgentTags))
					{
						UE_LOG(LogSeinSim, Warning, TEXT("ActivateAbility[%s]: target not reachable"),
							*Cmd.AbilityTag.ToString());
						RejectCommand(SeinARTSTags::Command_Reject_PathUnreachable);
						continue;
					}
				}
			}

			// 4. CanActivate escape hatch (after declarative validation)
			if (!Ability->CanActivate())
			{
				UE_LOG(LogSeinSim, Warning, TEXT("ActivateAbility[%s]: CanActivate returned false"),
					*Cmd.AbilityTag.ToString());
				RejectCommand(SeinARTSTags::Command_Reject_CanActivateFailed);
				continue;
			}

			// 5. Affordability (catalog-aware; consults CostDirection + SpendBehavior)
			if (!USeinResourceBPFL::SeinCanAfford(this, Cmd.PlayerID, Ability->ResourceCost))
			{
				UE_LOG(LogSeinSim, Warning, TEXT("ActivateAbility[%s]: unaffordable"),
					*Cmd.AbilityTag.ToString());
				RejectCommand(SeinARTSTags::Command_Reject_Unaffordable);
				continue;
			}

			// 6. Deduct cost (cleared on cancel-with-refund via USeinAbility::DeactivateAbility)
			USeinResourceBPFL::SeinDeduct(this, Cmd.PlayerID, Ability->ResourceCost);

			// 7a. Cancel OTHER abilities whose OwnedTags intersect this ability's
			// CancelAbilitiesWithTag. Explicitly skip self — matching self here
			// would cancel-then-reactivate on every duplicate command (e.g. a
			// broker dispatching the same move twice in one command frame), and
			// nothing actually moves. Self-cancelling-reissue is handled below.
			if (!Ability->CancelAbilitiesWithTag.IsEmpty())
			{
				for (USeinAbility* Other : AbilityComp->AbilityInstances)
				{
					if (Other && Other != Ability && Other->bIsActive &&
						Other->OwnedTags.HasAny(Ability->CancelAbilitiesWithTag))
					{
						Other->CancelAbility();
					}
				}
			}

			// 7b. Self-cancelling reissue: if this ability is already running and
			// lists any of its own OwnedTags in CancelAbilitiesWithTag, the
			// designer is asking "re-issuing me should kill the previous run
			// before the new one starts" — so cancel the prior activation
			// before ActivateAbility spins up a fresh one.
			if (Ability->bIsActive &&
				Ability->OwnedTags.HasAny(Ability->CancelAbilitiesWithTag))
			{
				Ability->CancelAbility();
			}

			// 8. Stamp the deducted snapshot and commit activation.
			Ability->RecordDeductedCost(Ability->ResourceCost);
			Ability->ActivateAbility(Cmd.TargetEntity, Cmd.TargetLocation);
			if (!Ability->bIsPassive)
			{
				AbilityComp->ActiveAbility = Ability;
			}
		}
		else if (Cmd.CommandType == SeinARTSTags::Command_Type_CancelAbility)
		{
			FSeinAbilityData* AbilityComp = GetComponent<FSeinAbilityData>(Cmd.EntityHandle);
			if (AbilityComp && AbilityComp->ActiveAbility && AbilityComp->ActiveAbility->bIsActive)
			{
				AbilityComp->ActiveAbility->CancelAbility();
				AbilityComp->ActiveAbility = nullptr;
			}
			else
			{
				RejectCommand(SeinARTSTags::Command_Reject_InvalidTarget);
			}
		}
		else if (Cmd.CommandType == SeinARTSTags::Command_Type_QueueProduction)
		{
			FSeinProductionData* ProdComp = GetComponent<FSeinProductionData>(Cmd.EntityHandle);
			if (!ProdComp) { RejectCommand(SeinARTSTags::Command_Reject_MissingComponent); continue; }
			if (!ProdComp->CanQueueMore()) { RejectCommand(SeinARTSTags::Command_Reject_QueueFull); continue; }

			// Find the producible class matching the command's AbilityTag vs CDO ArchetypeTag.
			TSubclassOf<ASeinActor> MatchedClass = nullptr;
			for (const TSubclassOf<ASeinActor>& ProdClass : ProdComp->ProducibleClasses)
			{
				if (!ProdClass) continue;
				const ASeinActor* ProdCDO = GetDefault<ASeinActor>(ProdClass);
				if (ProdCDO && ProdCDO->ArchetypeDefinition &&
					ProdCDO->ArchetypeDefinition->ArchetypeTag == Cmd.AbilityTag)
				{
					MatchedClass = ProdClass;
					break;
				}
			}
			if (!MatchedClass) { RejectCommand(SeinARTSTags::Command_Reject_InvalidTarget); continue; }

			const USeinArchetypeDefinition* ProdArchetype = GetDefault<ASeinActor>(MatchedClass)->ArchetypeDefinition;

			FSeinPlayerState* PS = GetPlayerStateMutable(Cmd.PlayerID);
			if (!PS) { RejectCommand(SeinARTSTags::Command_Reject_MissingComponent); continue; }
			if (!PS->HasAllPlayerTags(ProdArchetype->PrerequisiteTags)) { RejectCommand(SeinARTSTags::Command_Reject_BlockedByTag); continue; }

			// Split cost into AtEnqueue / AtCompletion buckets per the catalog
			// `ProductionDeductionTiming` of each resource (§9 Q1/Q6).
			const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
			FSeinResourceCost AtEnqueueCost;
			FSeinResourceCost AtCompletionCost;
			for (const auto& Pair : ProdArchetype->ProductionCost.Amounts)
			{
				const FSeinResourceDefinition* Def = Settings->ResourceCatalog.FindByPredicate(
					[&](const FSeinResourceDefinition& D) { return D.ResourceTag == Pair.Key; });
				const ESeinProductionDeductionTiming Timing = Def
					? Def->ProductionDeductionTiming
					: ESeinProductionDeductionTiming::AtEnqueue;
				if (Timing == ESeinProductionDeductionTiming::AtCompletion)
				{
					AtCompletionCost.Amounts.Add(Pair.Key, Pair.Value);
				}
				else
				{
					AtEnqueueCost.Amounts.Add(Pair.Key, Pair.Value);
				}
			}

			// Enqueue bucket must be affordable + catalog-legal (consults CostDirection).
			if (!USeinResourceBPFL::SeinCanAfford(this, Cmd.PlayerID, AtEnqueueCost)) { RejectCommand(SeinARTSTags::Command_Reject_Unaffordable); continue; }
			USeinResourceBPFL::SeinDeduct(this, Cmd.PlayerID, AtEnqueueCost);

			FSeinProductionQueueEntry Entry;
			Entry.ActorClass = MatchedClass;
			Entry.TotalBuildTime = ProdArchetype->BuildTime;
			Entry.AtEnqueueCost = MoveTemp(AtEnqueueCost);
			Entry.AtCompletionCost = MoveTemp(AtCompletionCost);
			Entry.bIsResearch = ProdArchetype->bIsResearch;
			Entry.ResearchEffectClass = ProdArchetype->GrantedTechEffect;
			Entry.RefundPolicy = ProdArchetype->RefundPolicy;

			const bool bWasEmpty = ProdComp->Queue.Num() == 0;
			ProdComp->Queue.Add(MoveTemp(Entry));

			if (bWasEmpty)
			{
				ProdComp->CurrentBuildProgress = FFixedPoint::Zero;
				ProdComp->bStalledAtCompletion = false;
				EnqueueVisualEvent(FSeinVisualEvent::MakeProductionEvent(
					Cmd.EntityHandle, Cmd.AbilityTag, /*bCompleted=*/false));
			}
		}
		else if (Cmd.CommandType == SeinARTSTags::Command_Type_CancelProduction)
		{
			FSeinProductionData* ProdComp = GetComponent<FSeinProductionData>(Cmd.EntityHandle);
			if (!ProdComp) { RejectCommand(SeinARTSTags::Command_Reject_MissingComponent); continue; }

			const int32 CancelIdx = Cmd.QueueIndex;
			if (CancelIdx < 0 || CancelIdx >= ProdComp->Queue.Num()) { RejectCommand(SeinARTSTags::Command_Reject_InvalidTarget); continue; }

			// Refund AtEnqueueCost only (AtCompletion was never deducted). Policy
			// chooses between progress-proportional (default) and flat-custom.
			if (FSeinPlayerState* PS = GetPlayerStateMutable(Cmd.PlayerID))
			{
				const FSeinProductionQueueEntry& CancelledEntry = ProdComp->Queue[CancelIdx];

				FFixedPoint RefundFraction;
				if (CancelledEntry.RefundPolicy.bUseCustomRefund)
				{
					RefundFraction = CancelledEntry.RefundPolicy.CustomRefundPercentage;
				}
				else
				{
					// Progress-proportional: refund = (1 - progress) * cost.
					// Only the front entry has non-zero progress.
					FFixedPoint ProgressFraction = FFixedPoint::Zero;
					if (CancelIdx == 0 && CancelledEntry.TotalBuildTime > FFixedPoint::Zero)
					{
						ProgressFraction = ProdComp->CurrentBuildProgress / CancelledEntry.TotalBuildTime;
						if (ProgressFraction > FFixedPoint::One) ProgressFraction = FFixedPoint::One;
					}
					RefundFraction = FFixedPoint::One - ProgressFraction;
				}

				if (RefundFraction > FFixedPoint::Zero)
				{
					FSeinResourceCost Refund;
					Refund.Amounts.Reserve(CancelledEntry.AtEnqueueCost.Amounts.Num());
					for (const auto& Pair : CancelledEntry.AtEnqueueCost.Amounts)
					{
						Refund.Amounts.Add(Pair.Key, Pair.Value * RefundFraction);
					}
					USeinResourceBPFL::SeinRefund(this, Cmd.PlayerID, Refund);
				}
			}

			ProdComp->Queue.RemoveAt(CancelIdx);
			if (CancelIdx == 0)
			{
				ProdComp->CurrentBuildProgress = FFixedPoint::Zero;
				ProdComp->bStalledAtCompletion = false;
			}
		}
		else if (Cmd.CommandType == SeinARTSTags::Command_Type_SetRallyPoint)
		{
			FSeinProductionData* ProdComp = GetComponent<FSeinProductionData>(Cmd.EntityHandle);
			if (ProdComp)
			{
				if (Cmd.TargetEntity.IsValid())
				{
					ProdComp->RallyTarget.bIsEntityTarget = true;
					ProdComp->RallyTarget.EntityTarget = Cmd.TargetEntity;
					ProdComp->RallyTarget.Location = FFixedVector();
				}
				else
				{
					ProdComp->RallyTarget.bIsEntityTarget = false;
					ProdComp->RallyTarget.Location = Cmd.TargetLocation;
					ProdComp->RallyTarget.EntityTarget = FSeinEntityHandle();
				}
			}
			else
			{
				RejectCommand(SeinARTSTags::Command_Reject_MissingComponent);
			}
		}
		else
		{
			UE_LOG(LogSeinSim, Warning, TEXT("ProcessCommands: unknown command type %s"), *Cmd.CommandType.ToString());
			RejectCommand(SeinARTSTags::Command_Reject_InvalidTarget);
		}
	}

	PendingCommands.Clear();
}

void USeinWorldSubsystem::EnqueueCommand(const FSeinCommand& Command)
{
	PendingCommands.AddCommand(Command);
}

// ==================== Entity Management ====================

FSeinEntityHandle USeinWorldSubsystem::SpawnEntity(
	TSubclassOf<ASeinActor> ActorClass,
	const FFixedTransform& SpawnTransform,
	FSeinPlayerID OwnerPlayerID)
{
	if (!ActorClass)
	{
		UE_LOG(LogSeinSim, Error, TEXT("Cannot spawn entity: ActorClass is null"));
		return FSeinEntityHandle::Invalid();
	}

	// Read archetype data from the Blueprint CDO
	const ASeinActor* CDO = GetDefault<ASeinActor>(ActorClass);
	if (!CDO || !CDO->ArchetypeDefinition)
	{
		UE_LOG(LogSeinSim, Error, TEXT("Cannot spawn entity: Blueprint %s has no ArchetypeDefinition"), *ActorClass->GetName());
		return FSeinEntityHandle::Invalid();
	}

	const USeinArchetypeDefinition* ArchetypeDef = CDO->ArchetypeDefinition;

	FSeinEntityHandle Handle = EntityPool.Acquire(
		SpawnTransform,
		OwnerPlayerID
	);

	if (!Handle.IsValid())
	{
		UE_LOG(LogSeinSim, Error, TEXT("Failed to acquire entity from pool"));
		return FSeinEntityHandle::Invalid();
	}

	// Store actor class for bridge spawning
	EntityActorClassMap.Add(Handle, ActorClass);

	// Walk the Blueprint CDO's USeinActorComponent subobjects and inject each
	// one's sim payload into deterministic storage. Designers author sim data
	// via typed wrapper ACs (USeinAbilitiesComponent, USeinMovementComponent,
	// ...) or via USeinStructComponent + a designer-authored UDS (§2).
	//
	// NB: AActor::GetComponents() on a CDO only sees native CreateDefaultSubobject
	// components — BP-editor-added components live on the SCS. The helper below
	// walks native components + SCS nodes up the BP hierarchy in a stable order.
	TArray<const USeinActorComponent*> SimACs;
	AActor::GetActorClassDefaultComponents<USeinActorComponent>(ActorClass, SimACs);
	for (const USeinActorComponent* AC : SimACs)
	{
		if (!AC) continue;
		const FInstancedStruct Payload = AC->GetSimComponent();
		if (!Payload.IsValid()) continue;

		UScriptStruct* ComponentType = const_cast<UScriptStruct*>(Payload.GetScriptStruct());
		ISeinComponentStorage* Storage = GetOrCreateStorageForType(ComponentType);
		if (Storage)
		{
			Storage->AddComponent(Handle, Payload.GetMemory());
		}
	}

	// Instantiate ability UObjects if the entity was granted any
	InitializeEntityAbilities(Handle);

	// Auto-tag entity with its archetype tag (for archetype-scope modifier matching)
	// then seed refcounts + the global tag index from the full BaseTags set.
	if (FSeinTagData* TagComp = GetComponent<FSeinTagData>(Handle))
	{
		if (ArchetypeDef->ArchetypeTag.IsValid())
		{
			TagComp->BaseTags.AddTag(ArchetypeDef->ArchetypeTag);
		}
		SeedEntityTagsFromBase(Handle);
	}

	// Fire spawn visual event
	EnqueueVisualEvent(FSeinVisualEvent::MakeSpawnEvent(Handle, SpawnTransform.GetLocation()));

	UE_LOG(LogSeinSim, Log, TEXT("Spawned entity %s from %s (owner: %s)"),
		*Handle.ToString(), *ActorClass->GetName(), *OwnerPlayerID.ToString());

	return Handle;
}

FSeinEntityHandle USeinWorldSubsystem::SpawnEntityFromPlacedActor(
	ASeinActor* PlacedActor,
	FSeinPlayerID OwnerPlayerID)
{
	if (!PlacedActor)
	{
		UE_LOG(LogSeinSim, Error, TEXT("SpawnEntityFromPlacedActor: null actor"));
		return FSeinEntityHandle::Invalid();
	}
	if (!PlacedActor->ArchetypeDefinition)
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("SpawnEntityFromPlacedActor: actor %s has no ArchetypeDefinition"),
			*PlacedActor->GetName());
		return FSeinEntityHandle::Invalid();
	}

	const USeinArchetypeDefinition* ArchetypeDef = PlacedActor->ArchetypeDefinition;

	// Sim transform = editor-baked snapshot. The conversion from float
	// FVector to FFixedVector ran once in the editor process when the
	// designer placed/moved the actor (`PostEditMove`); the int64 bits
	// were serialized to the .umap. We just read them here — no FromFloat
	// at runtime, so cross-arch clients (PC + ARM Mac + mobile + console)
	// land on identical sim transforms.
	//
	// Migration path: actors placed before `PlacedSimLocation` existed
	// have `bSimLocationBaked == false`. We log a warning and fall back
	// to FromFloat — single-platform tests still work, but cross-arch
	// lockstep needs the designer to re-save the level (or run the
	// "Bake Determinism Snapshots" menu when it lands).
	FFixedVector SimLocation;
	if (PlacedActor->bSimLocationBaked)
	{
		SimLocation = PlacedActor->PlacedSimLocation;
	}
	else
	{
		UE_LOG(LogSeinSim, Warning,
			TEXT("SpawnEntityFromPlacedActor: %s has no baked PlacedSimLocation — "
				 "falling back to runtime FromFloat. Re-save the level (or nudge "
				 "the actor in editor) to bake the snapshot. NOT cross-arch deterministic."),
			*PlacedActor->GetName());
		SimLocation = FFixedVector::FromVector(PlacedActor->GetActorLocation());
	}
	const FFixedTransform SimTransform(SimLocation);

	FSeinEntityHandle Handle = EntityPool.Acquire(SimTransform, OwnerPlayerID);
	if (!Handle.IsValid())
	{
		UE_LOG(LogSeinSim, Error,
			TEXT("SpawnEntityFromPlacedActor: pool.Acquire failed for %s"),
			*PlacedActor->GetName());
		return FSeinEntityHandle::Invalid();
	}

	EntityActorClassMap.Add(Handle, PlacedActor->GetClass());

	// Walk the LIVE actor's USeinActorComponents — captures per-instance
	// edits beyond CDO defaults. Designers can drop a placed actor and
	// tune its Vision Radius / Blocker Height / etc. on the level
	// instance; this path picks those up correctly.
	TArray<USeinActorComponent*> SimACs;
	PlacedActor->GetComponents<USeinActorComponent>(SimACs);
	for (USeinActorComponent* AC : SimACs)
	{
		if (!AC) continue;
		const FInstancedStruct Payload = AC->GetSimComponent();
		if (!Payload.IsValid()) continue;

		UScriptStruct* ComponentType = const_cast<UScriptStruct*>(Payload.GetScriptStruct());
		ISeinComponentStorage* Storage = GetOrCreateStorageForType(ComponentType);
		if (Storage)
		{
			Storage->AddComponent(Handle, Payload.GetMemory());
		}
	}

	InitializeEntityAbilities(Handle);

	if (FSeinTagData* TagComp = GetComponent<FSeinTagData>(Handle))
	{
		if (ArchetypeDef->ArchetypeTag.IsValid())
		{
			TagComp->BaseTags.AddTag(ArchetypeDef->ArchetypeTag);
		}
		SeedEntityTagsFromBase(Handle);
	}

	// Deliberately NO EnqueueVisualEvent — placed actors already exist in
	// the world. Firing EntitySpawned would make the actor bridge spawn a
	// second render actor in addition to the one the designer placed.

	UE_LOG(LogSeinSim, Log,
		TEXT("Auto-registered placed actor %s as entity %s (owner: %s)"),
		*PlacedActor->GetName(), *Handle.ToString(), *OwnerPlayerID.ToString());

	return Handle;
}

FSeinEntityHandle USeinWorldSubsystem::SpawnAbstractEntity(
	const FFixedTransform& SpawnTransform,
	FSeinPlayerID OwnerPlayerID)
{
	// Acquire a handle from the pool; no ActorClass = no render spawn, no
	// archetype walk, no ability initialization. The caller is on the hook to
	// add whatever components the abstract entity needs via AddComponent<T>.
	FSeinEntityHandle Handle = EntityPool.Acquire(SpawnTransform, OwnerPlayerID);
	if (!Handle.IsValid())
	{
		UE_LOG(LogSeinSim, Error, TEXT("SpawnAbstractEntity: pool.Acquire failed"));
		return FSeinEntityHandle::Invalid();
	}
	// Intentionally no EntityActorClassMap entry — actor bridge no-ops on missing map entry.
	UE_LOG(LogSeinSim, Verbose, TEXT("Spawned abstract entity %s (owner: %s)"),
		*Handle.ToString(), *OwnerPlayerID.ToString());
	return Handle;
}

void USeinWorldSubsystem::DestroyEntity(FSeinEntityHandle Handle)
{
	if (!Handle.IsValid() || !EntityPool.IsValid(Handle))
	{
		return;
	}

	// Mark for deferred destruction
	FSeinEntity* Entity = EntityPool.Get(Handle);
	if (Entity)
	{
		Entity->SetAlive(false);
	}
	PendingDestroy.AddUnique(Handle);
}

void USeinWorldSubsystem::ProcessDeferredDestroys()
{
	for (const FSeinEntityHandle& Handle : PendingDestroy)
	{
		if (!EntityPool.IsValid(Handle)) continue;

		// Cancel any active abilities/latent actions
		if (LatentActionManager)
		{
			LatentActionManager->CancelActionsForEntity(Handle);
		}

		// Strip any effects this entity was the source of, where the effect class
		// declares bRemoveOnSourceDeath (DESIGN §8 Q4c). Runs BEFORE component
		// storages + pool release so downstream consumers can still read the
		// effect's state while the removal hooks fire.
		RemoveEffectsFromDeadSource(Handle);

		// Containment death propagation (DESIGN §14) runs before storages clear so
		// PropagateContainerDeath can still read the container's Occupants list +
		// OnEject/OnContainerDeath effect classes off FSeinContainmentData.
		if (GetComponent<FSeinContainmentData>(Handle))
		{
			PropagateContainerDeath(Handle);
		}

		// Member-side: if the dying entity is contained, evict it from its
		// container's Occupants + CurrentLoad / VisualSlotAssignments / attachment
		// slot. Mirrors the CommandBroker eviction below.
		if (const FSeinContainmentMemberData* MemComp = GetComponent<FSeinContainmentMemberData>(Handle))
		{
			if (EntityPool.IsValid(MemComp->CurrentContainer))
			{
				if (FSeinContainmentData* Container = GetComponent<FSeinContainmentData>(MemComp->CurrentContainer))
				{
					Container->Occupants.Remove(Handle);
					Container->CurrentLoad = FMath::Max(0, Container->CurrentLoad - MemComp->Size);
					if (Container->bTracksVisualSlots)
					{
						const int32 Idx = MemComp->VisualSlotIndex;
						if (Container->VisualSlotAssignments.IsValidIndex(Idx))
						{
							Container->VisualSlotAssignments[Idx] = FSeinEntityHandle();
						}
					}
					// Attachment slot (if any) — clear assignment + fire visual event.
					if (MemComp->CurrentSlot.IsValid())
					{
						if (FSeinAttachmentSpec* Spec = GetComponent<FSeinAttachmentSpec>(MemComp->CurrentContainer))
						{
							Spec->Assignments.Remove(MemComp->CurrentSlot);
						}
						EnqueueVisualEvent(FSeinVisualEvent::MakeAttachmentSlotEmptiedEvent(
							MemComp->CurrentContainer, Handle, MemComp->CurrentSlot));
					}
					// Death of a contained entity doesn't spawn an exit-location event
					// — container dying with eject=false funnels through
					// PropagateContainerDeath above; death of just one occupant inside
					// a still-living container is a quieter cleanup (no world teleport).
				}
			}
		}

		// Evict from the dying entity's current broker (DESIGN §5). If this leaves
		// the broker with no members and no queued orders, cull it via DestroyEntity
		// — it'll be processed on the next tick's PostTick.
		if (const FSeinBrokerMembershipData* Memb = GetComponent<FSeinBrokerMembershipData>(Handle))
		{
			if (EntityPool.IsValid(Memb->CurrentBrokerHandle))
			{
				if (FSeinCommandBrokerData* Broker = GetComponent<FSeinCommandBrokerData>(Memb->CurrentBrokerHandle))
				{
					Broker->Members.Remove(Handle);
					Broker->bCapabilityMapDirty = true;
					if (Broker->Members.Num() == 0 && Broker->OrderQueue.Num() == 0 && !Broker->bIsExecuting)
					{
						DestroyEntity(Memb->CurrentBrokerHandle);
					}
				}
			}
		}

		// Clear the entity from the global tag index and the named registry
		// before component storages are freed (UnindexEntityTags reads FSeinTagData).
		UnindexEntityTags(Handle);
		UnregisterHandleFromNames(Handle);

		// Remove all components
		for (auto& Pair : ComponentStorages)
		{
			Pair.Value->RemoveAllForEntity(Handle);
		}

		EnqueueVisualEvent(FSeinVisualEvent::MakeDestroyEvent(Handle));
		EntityActorClassMap.Remove(Handle);
		EntityPool.Release(Handle);

		UE_LOG(LogSeinSim, Log, TEXT("Destroyed entity %s"), *Handle.ToString());
	}

	PendingDestroy.Empty();
}

FSeinEntity* USeinWorldSubsystem::GetEntity(FSeinEntityHandle Handle)
{
	return EntityPool.Get(Handle);
}

const FSeinEntity* USeinWorldSubsystem::GetEntity(FSeinEntityHandle Handle) const
{
	return EntityPool.Get(Handle);
}

bool USeinWorldSubsystem::IsEntityAlive(FSeinEntityHandle Handle) const
{
	const FSeinEntity* Entity = EntityPool.Get(Handle);
	return Entity && Entity->IsAlive();
}

FSeinPlayerID USeinWorldSubsystem::GetEntityOwner(FSeinEntityHandle Handle) const
{
	return EntityPool.GetOwner(Handle);
}

void USeinWorldSubsystem::SetEntityOwner(FSeinEntityHandle Handle, FSeinPlayerID NewOwner)
{
	EntityPool.SetOwner(Handle, NewOwner);
}

TSubclassOf<ASeinActor> USeinWorldSubsystem::GetEntityActorClass(FSeinEntityHandle Handle) const
{
	const TSubclassOf<ASeinActor>* Found = EntityActorClassMap.Find(Handle);
	return Found ? *Found : nullptr;
}

FSeinPlayerState* USeinWorldSubsystem::GetPlayerStateMutable(FSeinPlayerID PlayerID)
{
	return PlayerStates.Find(PlayerID);
}

// ==================== Player & Faction ====================

void USeinWorldSubsystem::RegisterPlayer(FSeinPlayerID PlayerID, FSeinFactionID FactionID, uint8 TeamID)
{
	if (PlayerStates.Contains(PlayerID))
	{
		UE_LOG(LogSeinSim, Warning, TEXT("Player %s already registered"), *PlayerID.ToString());
		return;
	}

	FSeinPlayerState NewState(PlayerID, FactionID, TeamID);

	// Populate starting resources from the faction's ResourceKit, resolved
	// against the project-wide ResourceCatalog (catalog defaults + kit overrides).
	if (TObjectPtr<USeinFaction>* FactionPtr = Factions.Find(FactionID))
	{
		const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
		const TArray<FSeinResourceDefinition>& Catalog = Settings->ResourceCatalog;

		for (const FSeinFactionResourceEntry& KitEntry : (*FactionPtr)->ResourceKit)
		{
			if (!KitEntry.ResourceTag.IsValid()) { continue; }

			const FSeinResourceDefinition* CatalogEntry = Catalog.FindByPredicate(
				[&](const FSeinResourceDefinition& D) { return D.ResourceTag == KitEntry.ResourceTag; });

			const FFixedPoint StartingValue = KitEntry.bOverrideStartingValue
				? KitEntry.StartingValueOverride
				: (CatalogEntry ? CatalogEntry->DefaultStartingValue : FFixedPoint::Zero);

			const FFixedPoint Cap = KitEntry.bOverrideCap
				? KitEntry.CapOverride
				: (CatalogEntry ? CatalogEntry->DefaultCap : FFixedPoint::Zero);

			NewState.Resources.Add(KitEntry.ResourceTag, StartingValue);
			if (Cap > FFixedPoint::Zero)
			{
				NewState.ResourceCaps.Add(KitEntry.ResourceTag, Cap);
			}
		}
	}

	PlayerStates.Add(PlayerID, MoveTemp(NewState));

	UE_LOG(LogSeinSim, Log, TEXT("Registered player %s (faction: %s, team: %d)"),
		*PlayerID.ToString(), *FactionID.ToString(), TeamID);
}

FSeinPlayerState* USeinWorldSubsystem::GetPlayerState(FSeinPlayerID PlayerID)
{
	return PlayerStates.Find(PlayerID);
}

const FSeinPlayerState* USeinWorldSubsystem::GetPlayerState(FSeinPlayerID PlayerID) const
{
	return PlayerStates.Find(PlayerID);
}

bool USeinWorldSubsystem::GetPlayerStateCopy(FSeinPlayerID PlayerID, FSeinPlayerState& OutState) const
{
	const FSeinPlayerState* Found = PlayerStates.Find(PlayerID);
	if (Found)
	{
		OutState = *Found;
		return true;
	}
	return false;
}

void USeinWorldSubsystem::RegisterFaction(USeinFaction* Faction)
{
	if (!Faction) return;
	Factions.Add(Faction->FactionID, Faction);
	UE_LOG(LogSeinSim, Log, TEXT("Registered faction: %s"), *Faction->FactionName.ToString());
}

// ==================== Tags ====================

void USeinWorldSubsystem::GrantTag(FSeinEntityHandle Handle, FGameplayTag Tag)
{
	if (!Tag.IsValid()) return;
	FSeinTagData* TagComp = GetComponent<FSeinTagData>(Handle);
	if (!TagComp) return;

	if (TagComp->GrantTagInternal(Tag))
	{
		EntityTagIndex.FindOrAdd(Tag).Add(Handle);
	}
}

void USeinWorldSubsystem::UngrantTag(FSeinEntityHandle Handle, FGameplayTag Tag)
{
	if (!Tag.IsValid()) return;
	FSeinTagData* TagComp = GetComponent<FSeinTagData>(Handle);
	if (!TagComp) return;

	if (TagComp->UngrantTagInternal(Tag))
	{
		if (TArray<FSeinEntityHandle>* Bucket = EntityTagIndex.Find(Tag))
		{
			Bucket->RemoveSingle(Handle);
			if (Bucket->Num() == 0)
			{
				EntityTagIndex.Remove(Tag);
			}
		}
	}
}

// --- Player tags (refcounted, mirrors entity tag plumbing above) ---

void USeinWorldSubsystem::GrantPlayerTag(FSeinPlayerID PlayerID, FGameplayTag Tag)
{
	if (!Tag.IsValid()) return;
	FSeinPlayerState* State = GetPlayerStateMutable(PlayerID);
	if (!State) return;

	int32& Count = State->PlayerTagRefCounts.FindOrAdd(Tag);
	const int32 Old = Count++;
	if (Old == 0)
	{
		State->PlayerTags.AddTag(Tag);
	}
}

void USeinWorldSubsystem::UngrantPlayerTag(FSeinPlayerID PlayerID, FGameplayTag Tag)
{
	if (!Tag.IsValid()) return;
	FSeinPlayerState* State = GetPlayerStateMutable(PlayerID);
	if (!State) return;

	int32* Count = State->PlayerTagRefCounts.Find(Tag);
	if (!Count || *Count <= 0) return;

	--(*Count);
	if (*Count == 0)
	{
		State->PlayerTagRefCounts.Remove(Tag);
		State->PlayerTags.RemoveTag(Tag);
	}
}

bool USeinWorldSubsystem::AddBaseTag(FSeinEntityHandle Handle, FGameplayTag Tag)
{
	if (!Tag.IsValid()) return false;
	FSeinTagData* TagComp = GetComponent<FSeinTagData>(Handle);
	if (!TagComp) return false;
	if (TagComp->BaseTags.HasTagExact(Tag)) return false;

	TagComp->BaseTags.AddTag(Tag);
	GrantTag(Handle, Tag);
	return true;
}

bool USeinWorldSubsystem::RemoveBaseTag(FSeinEntityHandle Handle, FGameplayTag Tag)
{
	if (!Tag.IsValid()) return false;
	FSeinTagData* TagComp = GetComponent<FSeinTagData>(Handle);
	if (!TagComp) return false;
	if (!TagComp->BaseTags.HasTagExact(Tag)) return false;

	TagComp->BaseTags.RemoveTag(Tag);
	UngrantTag(Handle, Tag);
	return true;
}

void USeinWorldSubsystem::ReplaceBaseTags(FSeinEntityHandle Handle, const FGameplayTagContainer& NewBaseTags)
{
	FSeinTagData* TagComp = GetComponent<FSeinTagData>(Handle);
	if (!TagComp) return;

	// Diff old vs new. Touch refcounts only for tags that actually changed
	// membership in BaseTags — tags that persist keep their existing refcount.
	FGameplayTagContainer ToUngrant;
	for (const FGameplayTag& Existing : TagComp->BaseTags)
	{
		if (!NewBaseTags.HasTagExact(Existing))
		{
			ToUngrant.AddTag(Existing);
		}
	}
	FGameplayTagContainer ToGrant;
	for (const FGameplayTag& Incoming : NewBaseTags)
	{
		if (!TagComp->BaseTags.HasTagExact(Incoming))
		{
			ToGrant.AddTag(Incoming);
		}
	}

	TagComp->BaseTags = NewBaseTags;
	for (const FGameplayTag& Tag : ToUngrant) UngrantTag(Handle, Tag);
	for (const FGameplayTag& Tag : ToGrant)   GrantTag(Handle, Tag);
}

TArray<FSeinEntityHandle> USeinWorldSubsystem::GetEntitiesWithTag(FGameplayTag Tag) const
{
	if (const TArray<FSeinEntityHandle>* Bucket = EntityTagIndex.Find(Tag))
	{
		return *Bucket;
	}
	return {};
}

const TArray<FSeinEntityHandle>* USeinWorldSubsystem::FindEntitiesWithTag(FGameplayTag Tag) const
{
	return EntityTagIndex.Find(Tag);
}

// ==================== Named Entity Registry ====================

void USeinWorldSubsystem::RegisterNamedEntity(FName Name, FSeinEntityHandle Handle)
{
	if (Name.IsNone()) return;
	if (!EntityPool.IsValid(Handle)) return;
	NamedEntityRegistry.Add(Name, Handle);
}

FSeinEntityHandle USeinWorldSubsystem::LookupNamedEntity(FName Name) const
{
	if (Name.IsNone()) return FSeinEntityHandle::Invalid();
	if (const FSeinEntityHandle* Found = NamedEntityRegistry.Find(Name))
	{
		return *Found;
	}
	return FSeinEntityHandle::Invalid();
}

void USeinWorldSubsystem::UnregisterNamedEntity(FName Name)
{
	NamedEntityRegistry.Remove(Name);
}

// ==================== Attribute Resolution ====================

FFixedPoint USeinWorldSubsystem::ResolveAttribute(FSeinEntityHandle Handle, UScriptStruct* ComponentType, FName FieldName)
{
	ISeinComponentStorage* Storage = GetComponentStorageRaw(ComponentType);
	if (!Storage) return FFixedPoint::Zero;

	void* CompData = Storage->GetComponentRaw(Handle);
	if (!CompData) return FFixedPoint::Zero;

	const FFixedPoint BaseValue = FSeinAttributeResolver::ReadFixedPointField(CompData, ComponentType, FieldName);

	TArray<FSeinModifier> AllModifiers;

	// Instance-scope: walk the entity's active effects; CDO supplies the modifier list.
	if (const FSeinActiveEffectsData* EffectsComp = GetComponent<FSeinActiveEffectsData>(Handle))
	{
		for (const FSeinActiveEffect& Effect : EffectsComp->ActiveEffects)
		{
			const USeinEffect* Def = Effect.EffectClass ? GetDefault<USeinEffect>(Effect.EffectClass) : nullptr;
			if (!Def) continue;
			for (const FSeinModifier& Mod : Def->Modifiers)
			{
				if (Mod.TargetComponentType != ComponentType) continue;
				if (Mod.TargetFieldName != FieldName) continue;
				// Instance-scope modifiers (the effect's scope drives semantics; modifiers
				// on an Instance-scope effect are Instance by construction — the per-modifier
				// Scope field remains for legacy compatibility).
				for (int32 Stack = 0; Stack < Effect.CurrentStacks; ++Stack)
				{
					FSeinModifier& Added = AllModifiers.Add_GetRef(Mod);
					Added.SourceEntity = Effect.Source;
					Added.SourceEffectID = Effect.EffectInstanceID;
				}
			}
		}
	}

	const FSeinPlayerID OwnerID = GetEntityOwner(Handle);
	if (const FSeinPlayerState* PlayerState = GetPlayerState(OwnerID))
	{
		const FSeinTagData* TagComp = GetComponent<FSeinTagData>(Handle);
		const FGameplayTagContainer& EntityTags = TagComp ? TagComp->CombinedTags : FGameplayTagContainer::EmptyContainer;

		// Archetype-scope: iterate the player's archetype effects; CDO modifiers are filtered
		// by TargetArchetypeTag against the entity's tags.
		for (const FSeinActiveEffect& Effect : PlayerState->ArchetypeEffects)
		{
			const USeinEffect* Def = Effect.EffectClass ? GetDefault<USeinEffect>(Effect.EffectClass) : nullptr;
			if (!Def) continue;
			for (const FSeinModifier& Mod : Def->Modifiers)
			{
				if (Mod.TargetComponentType != ComponentType) continue;
				if (Mod.TargetFieldName != FieldName) continue;
				const FGameplayTag ArchTag = Mod.TargetArchetypeTag.IsValid()
					? Mod.TargetArchetypeTag
					: Def->DefaultTargetArchetypeTag;
				if (ArchTag.IsValid() && !EntityTags.HasTag(ArchTag))
				{
					continue;
				}
				for (int32 Stack = 0; Stack < Effect.CurrentStacks; ++Stack)
				{
					FSeinModifier& Added = AllModifiers.Add_GetRef(Mod);
					Added.SourceEntity = Effect.Source;
					Added.SourceEffectID = Effect.EffectInstanceID;
				}
			}
		}

		// Legacy `ArchetypeModifiers` flat list retired in Session 2.4 — tech-granted
		// archetype modifiers now flow through `ArchetypeEffects` above via the
		// unified effect pipeline (DESIGN §10).
	}

	if (AllModifiers.Num() == 0)
	{
		return BaseValue;
	}

	return FSeinAttributeResolver::ResolveModifiers(BaseValue, AllModifiers);
}

FFixedPoint USeinWorldSubsystem::ResolvePlayerAttribute(FSeinPlayerID PlayerID, UScriptStruct* StructType, FName FieldName) const
{
	const FSeinPlayerState* State = GetPlayerState(PlayerID);
	if (!State || !StructType)
	{
		return FFixedPoint::Zero;
	}

	// Base value — PlayerState itself is the only player-scope struct we reflect today;
	// future sub-structs (income rates, caps) can be targeted the same way.
	const void* BaseStruct = StructType == FSeinPlayerState::StaticStruct() ? static_cast<const void*>(State) : nullptr;
	FFixedPoint BaseValue = FFixedPoint::Zero;
	if (BaseStruct)
	{
		BaseValue = FSeinAttributeResolver::ReadFixedPointField(const_cast<void*>(BaseStruct), StructType, FieldName);
	}

	TArray<FSeinModifier> AllModifiers;
	for (const FSeinActiveEffect& Effect : State->PlayerEffects)
	{
		const USeinEffect* Def = Effect.EffectClass ? GetDefault<USeinEffect>(Effect.EffectClass) : nullptr;
		if (!Def) continue;
		for (const FSeinModifier& Mod : Def->Modifiers)
		{
			if (Mod.TargetComponentType != StructType) continue;
			if (Mod.TargetFieldName != FieldName) continue;
			for (int32 Stack = 0; Stack < Effect.CurrentStacks; ++Stack)
			{
				FSeinModifier& Added = AllModifiers.Add_GetRef(Mod);
				Added.SourceEntity = Effect.Source;
				Added.SourceEffectID = Effect.EffectInstanceID;
			}
		}
	}

	if (AllModifiers.Num() == 0)
	{
		return BaseValue;
	}
	return FSeinAttributeResolver::ResolveModifiers(BaseValue, AllModifiers);
}

// ==================== Effects ====================

uint32 USeinWorldSubsystem::ApplyEffect(FSeinEntityHandle Target, TSubclassOf<USeinEffect> EffectClass, FSeinEntityHandle Source)
{
	if (!EffectClass || !EntityPool.IsValid(Target))
	{
		return 0;
	}

	// If we're inside a sim tick, defer to the PreTick drain. Outside a sim tick
	// (render-side authored apply, test harness), commit synchronously. In shipping
	// SEIN_IS_SIM_CONTEXT() is always true (sim-only macro stripped) — correct
	// default since shipping applies run during the sim tick.
	if (SEIN_IS_SIM_CONTEXT())
	{
		PendingEffectApplies.Add({ Target, EffectClass, Source });
		return 0;
	}
	return ApplyEffectInternal(Target, EffectClass, Source);
}

void USeinWorldSubsystem::ProcessPendingEffectApplies()
{
	if (PendingEffectApplies.Num() == 0)
	{
		return;
	}
	// Swap-out the current queue so any applies-from-hooks land in a fresh queue
	// for the NEXT PreTick (per DESIGN §8 Q9c apply-batching).
	TArray<FSeinPendingEffectApply> Draining;
	Draining.Reserve(PendingEffectApplies.Num());
	Swap(Draining, PendingEffectApplies);

	for (const FSeinPendingEffectApply& P : Draining)
	{
		ApplyEffectInternal(P.Target, P.EffectClass, P.Source);
	}
}

uint32 USeinWorldSubsystem::ApplyEffectInternal(FSeinEntityHandle Target, TSubclassOf<USeinEffect> EffectClass, FSeinEntityHandle Source)
{
	if (!EffectClass || !EntityPool.IsValid(Target))
	{
		return 0;
	}
	const USeinEffect* CDO = GetDefault<USeinEffect>(EffectClass);
	if (!CDO)
	{
		return 0;
	}

	// --- Strip any existing effects matching RemoveEffectsWithTag ---
	for (const FGameplayTag& Tag : CDO->RemoveEffectsWithTag)
	{
		RemoveInstanceEffectsWithTag(Target, Tag);
	}

	// --- Prepare scope-specific storage pointers ---
	TArray<FSeinActiveEffect>* Storage = nullptr;
	uint32* IdCounter = nullptr;
	FSeinPlayerState* OwnerState = nullptr;
	FSeinActiveEffectsData* InstanceComp = nullptr;

	switch (CDO->Scope)
	{
		case ESeinModifierScope::Instance:
		{
			InstanceComp = GetComponent<FSeinActiveEffectsData>(Target);
			if (!InstanceComp) { return 0; }
			Storage = &InstanceComp->ActiveEffects;
			IdCounter = &InstanceComp->NextEffectInstanceID;
			break;
		}
		case ESeinModifierScope::Archetype:
		case ESeinModifierScope::Player:
		{
			const FSeinPlayerID OwnerID = GetEntityOwner(Target);
			OwnerState = GetPlayerStateMutable(OwnerID);
			if (!OwnerState) { return 0; }
			Storage = CDO->Scope == ESeinModifierScope::Archetype
				? &OwnerState->ArchetypeEffects
				: &OwnerState->PlayerEffects;
			IdCounter = &OwnerState->NextEffectInstanceID;
			break;
		}
	}

	if (!Storage || !IdCounter) { return 0; }

	// --- Stacking ---
	FSeinActiveEffect* Existing = nullptr;
	if (CDO->StackingRule != ESeinEffectStackingRule::Independent)
	{
		for (FSeinActiveEffect& E : *Storage)
		{
			if (E.EffectClass == EffectClass) { Existing = &E; break; }
		}
	}

	uint32 AssignedID = 0;
	bool bIsNewInstance = false;

	if (Existing && CDO->StackingRule == ESeinEffectStackingRule::Stack)
	{
		// Stack re-apply: bump CurrentStacks and refresh duration; no new instance,
		// no OnApply hook, no additional GrantedTags refcount.
		Existing->CurrentStacks = FMath::Min(Existing->CurrentStacks + 1, CDO->MaxStacks);
		if (CDO->Duration > FFixedPoint::Zero)
		{
			Existing->RemainingDuration = CDO->Duration;
		}
		AssignedID = Existing->EffectInstanceID;
	}
	else if (Existing && CDO->StackingRule == ESeinEffectStackingRule::Refresh)
	{
		// Refresh re-apply: stacks stay at 1, duration refreshes. No OnApply.
		Existing->CurrentStacks = 1;
		if (CDO->Duration > FFixedPoint::Zero)
		{
			Existing->RemainingDuration = CDO->Duration;
		}
		AssignedID = Existing->EffectInstanceID;
	}
	else
	{
		// Independent, or no existing instance: reject if storage already at MaxStacks for this class.
		if (CDO->StackingRule == ESeinEffectStackingRule::Independent)
		{
			int32 Count = 0;
			for (const FSeinActiveEffect& E : *Storage)
			{
				if (E.EffectClass == EffectClass) ++Count;
			}
			if (Count >= CDO->MaxStacks)
			{
				return 0;
			}
		}

		// --- Dev-mode apply-count warning ---
#if !UE_BUILD_SHIPPING
		{
			const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
			const int32 Threshold = Settings ? Settings->EffectCountWarningThreshold : 256;
			const int32 BeforeCount = Storage->Num();
			if (Threshold > 0 && BeforeCount < Threshold && BeforeCount + 1 >= Threshold)
			{
				UE_LOG(LogSeinSim, Warning,
					TEXT("Effect apply count crossing threshold (%d) for target %s — possible runaway effect loop"),
					Threshold, *Target.ToString());
			}
		}
#endif

		FSeinActiveEffect NewEffect;
		NewEffect.EffectClass = EffectClass;
		NewEffect.Source = Source;
		NewEffect.Target = Target;
		NewEffect.CurrentStacks = 1;
		NewEffect.TimeSinceLastPeriodic = FFixedPoint::Zero;
		NewEffect.RemainingDuration = CDO->Duration;  // 0 = instant, <0 = infinite, >0 = finite
		NewEffect.EffectInstanceID = (*IdCounter)++;
		Storage->Add(NewEffect);
		AssignedID = NewEffect.EffectInstanceID;
		bIsNewInstance = true;

		// --- Grant tags (refcount) ---
		// Per DESIGN §10 tech unification:
		//   Instance scope → GrantedTags go to the target entity.
		//   Archetype / Player scope → EffectTag + GrantedTags go to the target owner's
		//     player-state tag set (refcounted via GrantPlayerTag).
		if (CDO->Scope == ESeinModifierScope::Instance)
		{
			for (const FGameplayTag& Tag : CDO->GrantedTags)
			{
				GrantTag(Target, Tag);
			}
		}
		else
		{
			const FSeinPlayerID Owner = GetEntityOwner(Target);
			if (CDO->EffectTag.IsValid())
			{
				GrantPlayerTag(Owner, CDO->EffectTag);
			}
			for (const FGameplayTag& Tag : CDO->GrantedTags)
			{
				GrantPlayerTag(Owner, Tag);
			}
		}
	}

	// OnApply fires only on new instances — Stack / Refresh re-applies are
	// "same effect, refreshed" and do not re-trigger the apply hook.
	if (bIsNewInstance)
	{
		USeinEffect* MutableCDO = Cast<USeinEffect>(EffectClass->GetDefaultObject());
		if (MutableCDO)
		{
			MutableCDO->OnApply(Target, Source);
		}
		EnqueueVisualEvent(FSeinVisualEvent::MakeEffectEvent(Target, CDO->EffectTag, /*bApplied=*/true));
	}

	// Instant effect (Duration == 0): remove immediately. OnApply already fired;
	// OnExpire is gated inside RemoveInstanceEffect to not fire for Duration==0
	// (no real expiration occurred).
	if (bIsNewInstance && CDO->Duration == FFixedPoint::Zero)
	{
		RemoveInstanceEffect(Target, AssignedID, /*bByExpiration=*/true);
	}

	return AssignedID;
}

bool USeinWorldSubsystem::RemoveInstanceEffect(FSeinEntityHandle Target, uint32 EffectInstanceID, bool bByExpiration)
{
	// Locate across all three scope storages (Instance on entity; Archetype / Player on owner).
	// An instance ID is only unique within its containing storage, so we check each.
	auto TryRemoveFromArray = [&](TArray<FSeinActiveEffect>& Storage, FSeinPlayerID PlayerForTags) -> bool
	{
		for (int32 i = 0; i < Storage.Num(); ++i)
		{
			if (Storage[i].EffectInstanceID == EffectInstanceID)
			{
				const FSeinActiveEffect Effect = Storage[i];
				Storage.RemoveAtSwap(i, EAllowShrinking::No);

				const USeinEffect* Def = Effect.EffectClass ? GetDefault<USeinEffect>(Effect.EffectClass) : nullptr;
				// Symmetrical ungrant — see ApplyEffectInternal grant branch.
				if (Def)
				{
					if (Def->Scope == ESeinModifierScope::Instance)
					{
						for (const FGameplayTag& Tag : Def->GrantedTags)
						{
							UngrantTag(Target, Tag);
						}
					}
					else
					{
						if (Def->EffectTag.IsValid())
						{
							UngrantPlayerTag(PlayerForTags, Def->EffectTag);
						}
						for (const FGameplayTag& Tag : Def->GrantedTags)
						{
							UngrantPlayerTag(PlayerForTags, Tag);
						}
					}
				}

				if (Def)
				{
					USeinEffect* MutableCDO = Cast<USeinEffect>(Effect.EffectClass->GetDefaultObject());
					if (MutableCDO)
					{
						// OnExpire fires only for effects that actually had a positive
						// tickable duration and reached zero. Instant (Duration == 0)
						// and infinite (Duration < 0) effects never fire OnExpire.
						const bool bHadRealDuration = Def->Duration > FFixedPoint::Zero;
						if (bByExpiration && bHadRealDuration)
						{
							MutableCDO->OnExpire(Target);
						}
						MutableCDO->OnRemoved(Target, bByExpiration);
					}
					EnqueueVisualEvent(FSeinVisualEvent::MakeEffectEvent(Target, Def->EffectTag, /*bApplied=*/false));
				}
				return true;
			}
		}
		return false;
	};

	const FSeinPlayerID OwnerID = GetEntityOwner(Target);
	if (FSeinActiveEffectsData* InstanceComp = GetComponent<FSeinActiveEffectsData>(Target))
	{
		if (TryRemoveFromArray(InstanceComp->ActiveEffects, OwnerID))
		{
			return true;
		}
	}

	if (FSeinPlayerState* OwnerState = GetPlayerStateMutable(OwnerID))
	{
		if (TryRemoveFromArray(OwnerState->ArchetypeEffects, OwnerID))
		{
			return true;
		}
		if (TryRemoveFromArray(OwnerState->PlayerEffects, OwnerID))
		{
			return true;
		}
	}
	return false;
}

void USeinWorldSubsystem::RemoveInstanceEffectsWithTag(FSeinEntityHandle Target, FGameplayTag Tag)
{
	if (!Tag.IsValid()) return;

	auto RemoveMatching = [&](TArray<FSeinActiveEffect>& Storage, FSeinPlayerID PlayerForTags)
	{
		for (int32 i = Storage.Num() - 1; i >= 0; --i)
		{
			const USeinEffect* Def = Storage[i].EffectClass ? GetDefault<USeinEffect>(Storage[i].EffectClass) : nullptr;
			if (!Def || !Def->EffectTag.MatchesTag(Tag)) continue;

			const FSeinActiveEffect Effect = Storage[i];
			Storage.RemoveAtSwap(i, EAllowShrinking::No);

			if (Def->Scope == ESeinModifierScope::Instance)
			{
				for (const FGameplayTag& GT : Def->GrantedTags)
				{
					UngrantTag(Target, GT);
				}
			}
			else
			{
				if (Def->EffectTag.IsValid())
				{
					UngrantPlayerTag(PlayerForTags, Def->EffectTag);
				}
				for (const FGameplayTag& GT : Def->GrantedTags)
				{
					UngrantPlayerTag(PlayerForTags, GT);
				}
			}

			USeinEffect* MutableCDO = Cast<USeinEffect>(Effect.EffectClass->GetDefaultObject());
			if (MutableCDO)
			{
				MutableCDO->OnRemoved(Target, /*bByExpiration=*/false);
			}
			EnqueueVisualEvent(FSeinVisualEvent::MakeEffectEvent(Target, Def->EffectTag, /*bApplied=*/false));
		}
	};

	const FSeinPlayerID OwnerID = GetEntityOwner(Target);
	if (FSeinActiveEffectsData* InstanceComp = GetComponent<FSeinActiveEffectsData>(Target))
	{
		RemoveMatching(InstanceComp->ActiveEffects, OwnerID);
	}
	if (FSeinPlayerState* OwnerState = GetPlayerStateMutable(OwnerID))
	{
		RemoveMatching(OwnerState->ArchetypeEffects, OwnerID);
		RemoveMatching(OwnerState->PlayerEffects, OwnerID);
	}
}

void USeinWorldSubsystem::RemoveEffectsFromDeadSource(FSeinEntityHandle DeadHandle)
{
	if (!DeadHandle.IsValid()) return;

	auto WantsSourceDeathRemoval = [](const TSubclassOf<USeinEffect>& Class) -> bool
	{
		const USeinEffect* Def = Class ? GetDefault<USeinEffect>(Class) : nullptr;
		return Def && Def->bRemoveOnSourceDeath;
	};

	// Collect first, remove after — RemoveInstanceEffect mutates the same storages
	// we're iterating, so buffering the hits keeps iteration stable.
	TArray<TPair<FSeinEntityHandle, uint32>> ToRemove;

	// Instance scope: every entity's FSeinActiveEffectsData.
	EntityPool.ForEachEntity([&](FSeinEntityHandle Handle, FSeinEntity& /*Entity*/)
	{
		const FSeinActiveEffectsData* EffectsComp = GetComponent<FSeinActiveEffectsData>(Handle);
		if (!EffectsComp) return;
		for (const FSeinActiveEffect& E : EffectsComp->ActiveEffects)
		{
			if (E.Source == DeadHandle && WantsSourceDeathRemoval(E.EffectClass))
			{
				ToRemove.Add({ Handle, E.EffectInstanceID });
			}
		}
	});

	// Archetype / Player scope: every player state.
	ForEachPlayerStateMutable([&](FSeinPlayerID /*PID*/, FSeinPlayerState& State)
	{
		for (const FSeinActiveEffect& E : State.ArchetypeEffects)
		{
			if (E.Source == DeadHandle && WantsSourceDeathRemoval(E.EffectClass))
			{
				ToRemove.Add({ E.Target, E.EffectInstanceID });
			}
		}
		for (const FSeinActiveEffect& E : State.PlayerEffects)
		{
			if (E.Source == DeadHandle && WantsSourceDeathRemoval(E.EffectClass))
			{
				ToRemove.Add({ E.Target, E.EffectInstanceID });
			}
		}
	});

	// bByExpiration=false — this is cancellation by source death, not natural expiry.
	for (const TPair<FSeinEntityHandle, uint32>& R : ToRemove)
	{
		RemoveInstanceEffect(R.Key, R.Value, /*bByExpiration=*/false);
	}
}

// ==================== Component Storage Helpers ====================

ISeinComponentStorage* USeinWorldSubsystem::GetComponentStorageRaw(UScriptStruct* StructType)
{
	ISeinComponentStorage** Found = ComponentStorages.Find(StructType);
	return Found ? *Found : nullptr;
}

const ISeinComponentStorage* USeinWorldSubsystem::GetComponentStorageRaw(UScriptStruct* StructType) const
{
	ISeinComponentStorage* const* Found = ComponentStorages.Find(StructType);
	return Found ? *Found : nullptr;
}

ISeinComponentStorage* USeinWorldSubsystem::GetOrCreateStorageForType(UScriptStruct* StructType)
{
	if (ISeinComponentStorage** Found = ComponentStorages.Find(StructType))
	{
		return *Found;
	}

	FSeinGenericComponentStorage* Storage = new FSeinGenericComponentStorage(StructType, EntityPool.GetCapacity());
	ComponentStorages.Add(StructType, Storage);

	UE_LOG(LogSeinSim, Verbose, TEXT("Created component storage for %s"), *StructType->GetName());
	return Storage;
}

// ==================== System Registration ====================

void USeinWorldSubsystem::RegisterSystem(ISeinSystem* System)
{
	if (System && !Systems.Contains(System))
	{
		Systems.Add(System);
		bSystemsSorted = false;
		UE_LOG(LogSeinSim, Log, TEXT("Registered system: %s (phase: %d, priority: %d)"),
			*System->GetSystemName().ToString(),
			static_cast<int32>(System->GetPhase()),
			System->GetPriority());
	}
}

void USeinWorldSubsystem::UnregisterSystem(ISeinSystem* System)
{
	Systems.Remove(System);
}

void USeinWorldSubsystem::SortSystemsIfNeeded()
{
	if (!bSystemsSorted)
	{
		Algo::Sort(Systems, [](const ISeinSystem* A, const ISeinSystem* B)
		{
			if (A->GetPhase() != B->GetPhase())
			{
				return static_cast<uint8>(A->GetPhase()) < static_cast<uint8>(B->GetPhase());
			}
			return A->GetPriority() < B->GetPriority();
		});
		bSystemsSorted = true;
	}
}

// ==================== Visual Events ====================

void USeinWorldSubsystem::EnqueueVisualEvent(const FSeinVisualEvent& Event)
{
	VisualEventQueue.Enqueue(Event);
}

TArray<FSeinVisualEvent> USeinWorldSubsystem::FlushVisualEvents()
{
	return VisualEventQueue.Flush();
}

// ==================== State Hashing ====================

namespace
{
	// Tag comparator — iteration order that's stable across processes.
	FORCEINLINE bool TagNameLess(const FGameplayTag& A, const FGameplayTag& B)
	{
		return A.GetTagName().Compare(B.GetTagName()) < 0;
	}

	// Walk a TMap<FGameplayTag, T> in sorted-by-name order + hash each
	// (tag, value) pair. Handles the cross-process-stable iteration the
	// audit flagged for pointer-keyed maps + any tag-keyed state that
	// contributes to the sim hash.
	template<typename ValueType, typename HashValueFn>
	FORCEINLINE void HashTagMap(uint32& Hash, const TMap<FGameplayTag, ValueType>& Map, HashValueFn HashVal)
	{
		TArray<FGameplayTag> Keys;
		Map.GetKeys(Keys);
		Keys.Sort(TagNameLess);
		for (const FGameplayTag& Key : Keys)
		{
			Hash = HashCombine(Hash, GetTypeHash(Key));
			Hash = HashCombine(Hash, HashVal(Map[Key]));
		}
	}

	// Hash a FSeinPlayerState field-by-field. The free-function
	// GetTypeHash(FSeinPlayerState) only covers PlayerID (it's a TMap-
	// key hash) — we need the full state for desync detection.
	uint32 HashPlayerStateFields(const FSeinPlayerState& State)
	{
		uint32 Hash = GetTypeHash(State.PlayerID);
		Hash = HashCombine(Hash, GetTypeHash(State.FactionID));
		Hash = HashCombine(Hash, GetTypeHash(State.TeamID));
		Hash = HashCombine(Hash, GetTypeHash(State.bEliminated));
		Hash = HashCombine(Hash, GetTypeHash(State.bReady));
		Hash = HashCombine(Hash, GetTypeHash(State.bIsSpectator));
		Hash = HashCombine(Hash, GetTypeHash(State.bIsAI));
		Hash = HashCombine(Hash, GetTypeHash(State.NextEffectInstanceID));

		HashTagMap(Hash, State.Resources,          [](const FFixedPoint& V) { return GetTypeHash(V); });
		HashTagMap(Hash, State.ResourceCaps,       [](const FFixedPoint& V) { return GetTypeHash(V); });
		HashTagMap(Hash, State.PlayerTagRefCounts, [](int32 V)              { return GetTypeHash(V); });
		HashTagMap(Hash, State.StatCounters,       [](const FFixedPoint& V) { return GetTypeHash(V); });

		// PlayerTags is the cached presence set mirroring PlayerTagRefCounts.
		// Hashing both catches drift between the refcount map and the cache
		// (a known silent-desync category in tag-based systems). Sort the
		// container's tags by name first — FGameplayTagContainer's natural
		// iteration order isn't guaranteed across processes.
		{
			TArray<FGameplayTag> Tags;
			State.PlayerTags.GetGameplayTagArray(Tags);
			Tags.Sort(TagNameLess);
			Hash = HashCombine(Hash, GetTypeHash(Tags.Num()));
			for (const FGameplayTag& T : Tags)
			{
				Hash = HashCombine(Hash, GetTypeHash(T));
			}
		}

		// ArchetypeEffects + PlayerEffects are TArrays — order is already
		// deterministic by insertion (apply order is sim-tick driven).
		Hash = HashCombine(Hash, GetTypeHash(State.ArchetypeEffects.Num()));
		for (const FSeinActiveEffect& E : State.ArchetypeEffects)
		{
			Hash = HashCombine(Hash, GetTypeHash(E));
		}
		Hash = HashCombine(Hash, GetTypeHash(State.PlayerEffects.Num()));
		for (const FSeinActiveEffect& E : State.PlayerEffects)
		{
			Hash = HashCombine(Hash, GetTypeHash(E));
		}
		return Hash;
	}
}

int32 USeinWorldSubsystem::ComputeStateHash() const
{
	uint32 Hash = GetTypeHash(CurrentTick);

	// Entities — pool iterates in slot-index order, already deterministic.
	EntityPool.ForEachEntity([&Hash](FSeinEntityHandle Handle, const FSeinEntity& Entity)
	{
		Hash = HashCombine(Hash, GetTypeHash(Entity));
	});

	// Component storages — TMap is keyed by UScriptStruct* (pointer).
	// Pointer hash is stable within a process but not guaranteed across
	// processes, so sort by struct name before hashing. Keeps cross-process
	// comparison reliable for desync detection.
	{
		TArray<UScriptStruct*> Structs;
		Structs.Reserve(ComponentStorages.Num());
		for (const auto& Pair : ComponentStorages) { Structs.Add(Pair.Key); }
		Structs.Sort([](const UScriptStruct& A, const UScriptStruct& B)
		{
			return A.GetFName().Compare(B.GetFName()) < 0;
		});
		for (UScriptStruct* Struct : Structs)
		{
			Hash = HashCombine(Hash, GetTypeHash(Struct->GetFName()));
			Hash = HashCombine(Hash, ComponentStorages[Struct]->ComputeHash());
		}
	}

	// Player states — sort by PlayerID (uint8, ordering trivially stable).
	{
		TArray<FSeinPlayerID> Keys;
		PlayerStates.GetKeys(Keys);
		Keys.Sort();
		for (const FSeinPlayerID& PID : Keys)
		{
			Hash = HashCombine(Hash, HashPlayerStateFields(PlayerStates[PID]));
		}
	}

	// Entity tag index — sorted by tag name. Redundant with per-entity
	// FSeinTagData (which is in component storage), but hashing it catches
	// index/refcount drift the per-entity path would miss.
	{
		TArray<FGameplayTag> Keys;
		EntityTagIndex.GetKeys(Keys);
		Keys.Sort(TagNameLess);
		for (const FGameplayTag& Tag : Keys)
		{
			Hash = HashCombine(Hash, GetTypeHash(Tag));
			const TArray<FSeinEntityHandle>& Bucket = EntityTagIndex[Tag];
			Hash = HashCombine(Hash, GetTypeHash(Bucket.Num()));
			for (const FSeinEntityHandle& H : Bucket)
			{
				Hash = HashCombine(Hash, GetTypeHash(H));
			}
		}
	}

	// Named entity registry — sort by name.
	{
		TArray<FName> Keys;
		NamedEntityRegistry.GetKeys(Keys);
		Keys.Sort([](const FName& A, const FName& B) { return A.Compare(B) < 0; });
		for (const FName& Name : Keys)
		{
			Hash = HashCombine(Hash, GetTypeHash(Name));
			Hash = HashCombine(Hash, GetTypeHash(NamedEntityRegistry[Name]));
		}
	}

	// Diplomacy — relations + tag index. Sort by (From, To) pair.
	// FGameplayTagContainer has no GetTypeHash; iterate its tags sorted.
	{
		TArray<FSeinDiplomacyKey> Keys;
		DiplomacyRelations.GetKeys(Keys);
		Keys.Sort([](const FSeinDiplomacyKey& A, const FSeinDiplomacyKey& B)
		{
			if (A.FromPlayer.Value != B.FromPlayer.Value) return A.FromPlayer.Value < B.FromPlayer.Value;
			return A.ToPlayer.Value < B.ToPlayer.Value;
		});
		for (const FSeinDiplomacyKey& K : Keys)
		{
			Hash = HashCombine(Hash, GetTypeHash(K));
			const FGameplayTagContainer& Tags = DiplomacyRelations[K];
			TArray<FGameplayTag> TagList;
			Tags.GetGameplayTagArray(TagList);
			TagList.Sort(TagNameLess);
			for (const FGameplayTag& T : TagList)
			{
				Hash = HashCombine(Hash, GetTypeHash(T));
			}
		}
	}
	{
		TArray<FGameplayTag> Keys;
		DiplomacyTagIndex.GetKeys(Keys);
		Keys.Sort(TagNameLess);
		for (const FGameplayTag& Tag : Keys)
		{
			Hash = HashCombine(Hash, GetTypeHash(Tag));
			const FSeinDiplomacyIndexBucket& Bucket = DiplomacyTagIndex[Tag];
			Hash = HashCombine(Hash, GetTypeHash(Bucket.Pairs.Num()));
			for (const FSeinDiplomacyKey& K : Bucket.Pairs)
			{
				Hash = HashCombine(Hash, GetTypeHash(K));
			}
		}
	}

	// Active votes — sort by vote type tag. Inner Votes map sorted by player ID.
	{
		TArray<FGameplayTag> Keys;
		ActiveVotes.GetKeys(Keys);
		Keys.Sort(TagNameLess);
		for (const FGameplayTag& VTag : Keys)
		{
			const FSeinVoteState& V = ActiveVotes[VTag];
			Hash = HashCombine(Hash, GetTypeHash(VTag));
			Hash = HashCombine(Hash, GetTypeHash(V.RequiredThreshold));
			Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(V.Resolution)));
			Hash = HashCombine(Hash, GetTypeHash(V.InitiatedAtTick));
			Hash = HashCombine(Hash, GetTypeHash(V.ExpiresAtTick));
			Hash = HashCombine(Hash, GetTypeHash(V.Initiator));
			TArray<FSeinPlayerID> Voters;
			V.Votes.GetKeys(Voters);
			Voters.Sort();
			for (const FSeinPlayerID& Voter : Voters)
			{
				Hash = HashCombine(Hash, GetTypeHash(Voter));
				Hash = HashCombine(Hash, GetTypeHash(V.Votes[Voter]));
			}
		}
	}

	// Pending destroys — order matters if destroys trigger same-tick effects.
	Hash = HashCombine(Hash, GetTypeHash(PendingDestroy.Num()));
	for (const FSeinEntityHandle& H : PendingDestroy)
	{
		Hash = HashCombine(Hash, GetTypeHash(H));
	}

	// Sim PRNG cursor — determinism of any roll-ordered systems depends on
	// it advancing identically on all clients. Hash both state halves.
	Hash = HashCombine(Hash, GetTypeHash(SimRandom.State0));
	Hash = HashCombine(Hash, GetTypeHash(SimRandom.State1));

	// Match + pause flags.
	Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(MatchState)));
	Hash = HashCombine(Hash, GetTypeHash(bSimPaused));
	Hash = HashCombine(Hash, GetTypeHash(bSimPausedHard));

	return static_cast<int32>(Hash);
}

// ==================== Ability Initialization ====================

void USeinWorldSubsystem::InitializeEntityAbilities(FSeinEntityHandle Handle)
{
	FSeinAbilityData* AbilityComp = GetComponent<FSeinAbilityData>(Handle);
	if (!AbilityComp)
	{
		// Not every entity has abilities (projectiles, static props, resource piles);
		// this is expected, not an error.
		return;
	}

	for (const TSubclassOf<USeinAbility>& AbilityClass : AbilityComp->GrantedAbilityClasses)
	{
		if (!AbilityClass)
		{
			UE_LOG(LogSeinSim, Warning, TEXT("Entity %s: null ability class in GrantedAbilityClasses"),
				*Handle.ToString());
			continue;
		}

		USeinAbility* AbilityInstance = NewObject<USeinAbility>(this, AbilityClass);
		AbilityInstance->InitializeAbility(Handle, this);
		AbilityComp->AbilityInstances.Add(AbilityInstance);

		UE_LOG(LogSeinSim, Verbose, TEXT("Entity %s: granted ability %s [tag=%s passive=%d]"),
			*Handle.ToString(),
			*AbilityClass->GetName(),
			*AbilityInstance->AbilityTag.ToString(),
			AbilityInstance->bIsPassive ? 1 : 0);

		if (AbilityInstance->bIsPassive)
		{
			AbilityInstance->ActivateAbility(Handle, FFixedVector::ZeroVector);
			AbilityComp->ActivePassives.Add(AbilityInstance);
		}
	}
}

// ==================== Tag seeding / unindexing ====================

void USeinWorldSubsystem::SeedEntityTagsFromBase(FSeinEntityHandle Handle)
{
	FSeinTagData* TagComp = GetComponent<FSeinTagData>(Handle);
	if (!TagComp) return;

	// Snapshot first — GrantTag doesn't touch BaseTags, but a stable iteration
	// source is cheap and makes the intent obvious.
	TArray<FGameplayTag> SeedTags;
	TagComp->BaseTags.GetGameplayTagArray(SeedTags);
	for (const FGameplayTag& Tag : SeedTags)
	{
		GrantTag(Handle, Tag);
	}
}

void USeinWorldSubsystem::UnindexEntityTags(FSeinEntityHandle Handle)
{
	const FSeinTagData* TagComp = GetComponent<FSeinTagData>(Handle);
	if (!TagComp) return;

	for (const TPair<FGameplayTag, int32>& Pair : TagComp->TagRefCounts)
	{
		if (Pair.Value <= 0) continue;
		if (TArray<FSeinEntityHandle>* Bucket = EntityTagIndex.Find(Pair.Key))
		{
			Bucket->RemoveSingle(Handle);
			if (Bucket->Num() == 0)
			{
				EntityTagIndex.Remove(Pair.Key);
			}
		}
	}
}

void USeinWorldSubsystem::UnregisterHandleFromNames(FSeinEntityHandle Handle)
{
	for (auto It = NamedEntityRegistry.CreateIterator(); It; ++It)
	{
		if (It.Value() == Handle)
		{
			It.RemoveCurrent();
		}
	}
}

// ==================== CommandBroker helpers (DESIGN §5) ====================

FSeinEntityHandle USeinWorldSubsystem::FindSharedBroker(const TArray<FSeinEntityHandle>& Members) const
{
	if (Members.Num() == 0) return FSeinEntityHandle::Invalid();

	FSeinEntityHandle Shared;
	for (const FSeinEntityHandle& M : Members)
	{
		const FSeinBrokerMembershipData* Memb = GetComponent<FSeinBrokerMembershipData>(M);
		if (!Memb || !Memb->CurrentBrokerHandle.IsValid())
		{
			return FSeinEntityHandle::Invalid();
		}
		if (!Shared.IsValid())
		{
			Shared = Memb->CurrentBrokerHandle;
		}
		else if (Shared != Memb->CurrentBrokerHandle)
		{
			return FSeinEntityHandle::Invalid();
		}
	}
	return Shared;
}

FSeinEntityHandle USeinWorldSubsystem::CreateBrokerForMembers(
	const TArray<FSeinEntityHandle>& FilteredMembers,
	FSeinPlayerID OwnerPlayerID,
	const FSeinBrokerQueuedOrder& FirstOrder)
{
	if (FilteredMembers.Num() == 0) return FSeinEntityHandle::Invalid();

	// 1. Evict each member from its prior broker (one-broker-per-member invariant).
	for (const FSeinEntityHandle& M : FilteredMembers)
	{
		FSeinBrokerMembershipData* Memb = GetComponent<FSeinBrokerMembershipData>(M);
		if (!Memb || !Memb->CurrentBrokerHandle.IsValid()) continue;
		if (!EntityPool.IsValid(Memb->CurrentBrokerHandle)) continue;
		FSeinCommandBrokerData* OldBroker = GetComponent<FSeinCommandBrokerData>(Memb->CurrentBrokerHandle);
		if (!OldBroker) continue;
		OldBroker->Members.Remove(M);
		OldBroker->bCapabilityMapDirty = true;
		if (OldBroker->Members.Num() == 0 && OldBroker->OrderQueue.Num() == 0 && !OldBroker->bIsExecuting)
		{
			DestroyEntity(Memb->CurrentBrokerHandle);
		}
	}

	// 2. Compute initial centroid.
	FFixedVector InitialCentroid;
	int32 CentroidCount = 0;
	for (const FSeinEntityHandle& M : FilteredMembers)
	{
		if (const FSeinEntity* E = GetEntity(M))
		{
			InitialCentroid += E->Transform.GetLocation();
			++CentroidCount;
		}
	}
	if (CentroidCount > 0)
	{
		InitialCentroid = InitialCentroid / FFixedPoint::FromInt(CentroidCount);
	}

	// 3. Spawn the abstract broker entity.
	FSeinEntityHandle BrokerHandle = SpawnAbstractEntity(FFixedTransform(InitialCentroid), OwnerPlayerID);
	if (!BrokerHandle.IsValid()) return FSeinEntityHandle::Invalid();

	// 4. Build + inject FSeinCommandBrokerData with the first order pre-queued.
	FSeinCommandBrokerData BrokerData;
	BrokerData.Members = FilteredMembers;
	BrokerData.Centroid = InitialCentroid;
	BrokerData.Anchor = FirstOrder.TargetLocation;
	BrokerData.OrderQueue.Add(FirstOrder);
	BrokerData.bCapabilityMapDirty = true;

	// Resolver class resolution: plugin-setting soft class → framework default.
	TSubclassOf<USeinCommandBrokerResolver> ResolverClass;
	if (const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>())
	{
		if (!Settings->DefaultBrokerResolverClass.IsNull())
		{
			ResolverClass = Settings->DefaultBrokerResolverClass.LoadSynchronous();
		}
	}
	if (!ResolverClass || ResolverClass->HasAnyClassFlags(CLASS_Abstract))
	{
		ResolverClass = USeinDefaultCommandBrokerResolver::StaticClass();
	}
	BrokerData.Resolver = NewObject<USeinCommandBrokerResolver>(this, ResolverClass);

	AddComponent(BrokerHandle, BrokerData);

	// 5. Update each member's back-reference. Create the component if missing.
	for (const FSeinEntityHandle& M : FilteredMembers)
	{
		FSeinBrokerMembershipData* Memb = GetComponent<FSeinBrokerMembershipData>(M);
		if (Memb)
		{
			Memb->CurrentBrokerHandle = BrokerHandle;
		}
		else
		{
			FSeinBrokerMembershipData NewMemb;
			NewMemb.CurrentBrokerHandle = BrokerHandle;
			AddComponent(M, NewMemb);
		}
	}

	// 6. Inline-dispatch the first order (skips the 1-tick delay of waiting for
	// SeinCommandBrokerSystem's PostTick pass). Subsequent queue advancement runs
	// through the system.
	if (FSeinCommandBrokerData* BrokerPtr = GetComponent<FSeinCommandBrokerData>(BrokerHandle))
	{
		SeinCommandBrokerDispatch::DispatchFrontOrder(*this, BrokerHandle, *BrokerPtr);
	}

	return BrokerHandle;
}

// ==================== Match Flow (DESIGN §18) ====================

void USeinWorldSubsystem::SetSimPaused(bool bPaused, ESeinPauseMode PauseMode)
{
	const bool bWasPaused = bSimPaused;
	bSimPaused = bPaused;
	bSimPausedHard = bPaused && (PauseMode == ESeinPauseMode::Hard);

	// Fire match-flow visual events for UI / scenario subscribers. Suppress
	// double-fires when the state didn't actually change.
	if (bWasPaused != bPaused)
	{
		if (bPaused)
		{
			if (MatchState == ESeinMatchState::Playing)
			{
				MatchState = ESeinMatchState::Paused;
			}
			EnqueueVisualEvent(FSeinVisualEvent::MakeMatchFlowEvent(ESeinVisualEventType::MatchPaused));
		}
		else
		{
			if (MatchState == ESeinMatchState::Paused)
			{
				MatchState = ESeinMatchState::Playing;
			}
			EnqueueVisualEvent(FSeinVisualEvent::MakeMatchFlowEvent(ESeinVisualEventType::MatchResumed));
		}
	}
}

void USeinWorldSubsystem::StartMatch(const FSeinMatchSettings& Settings)
{
	if (MatchState != ESeinMatchState::Lobby && MatchState != ESeinMatchState::Ended)
	{
		UE_LOG(LogSeinSim, Warning, TEXT("StartMatch: ignored — match state is already %d"),
			static_cast<int32>(MatchState));
		return;
	}
	// Snapshot settings — immutable from this point until next StartMatch.
	CurrentMatchSettings = Settings;
	MatchState = ESeinMatchState::Starting;

	// Pre-match countdown deadline in sim ticks. Convert the fixed-point seconds
	// value to ticks via the configured tick rate. Minimum 1 tick to ensure the
	// Starting state is observable even with zero countdown setting.
	const USeinARTSCoreSettings* CoreSettings = GetDefault<USeinARTSCoreSettings>();
	const int32 TickRate = CoreSettings ? CoreSettings->SimulationTickRate : 30;
	const FFixedPoint Seconds = Settings.PreMatchCountdown;
	// Fixed-point → int truncation with ceil on any fractional part.
	int32 CountdownTicks = static_cast<int32>((Seconds * FFixedPoint::FromInt(TickRate)).ToInt());
	if (Seconds > FFixedPoint::Zero && CountdownTicks <= 0) CountdownTicks = 1;
	StartingStateDeadlineTick = CurrentTick + CountdownTicks;

	EnqueueVisualEvent(FSeinVisualEvent::MakeMatchFlowEvent(ESeinVisualEventType::MatchStarting));
	UE_LOG(LogSeinSim, Log, TEXT("StartMatch: Starting state begins, deadline at tick %d"), StartingStateDeadlineTick);
}

void USeinWorldSubsystem::EndMatch(FSeinPlayerID Winner, FGameplayTag Reason)
{
	if (MatchState == ESeinMatchState::Ended || MatchState == ESeinMatchState::Lobby)
	{
		return; // nothing in-flight to end
	}
	MatchState = ESeinMatchState::Ending;
	EnqueueVisualEvent(FSeinVisualEvent::MakeMatchFlowEvent(ESeinVisualEventType::MatchEnding, Winner, Reason));

	// Immediate Ending → Ended transition in V1; designers that want a staged
	// cleanup phase (fade-out cinematic, score screen pause) can schedule work
	// during Ending via scenario abilities, then route to a follow-up command
	// to finalize. Minimal polish can land when the first real game asks.
	MatchState = ESeinMatchState::Ended;
	EnqueueVisualEvent(FSeinVisualEvent::MakeMatchFlowEvent(ESeinVisualEventType::MatchEnded, Winner, Reason));
	UE_LOG(LogSeinSim, Log, TEXT("EndMatch: Winner=%s Reason=%s"),
		*Winner.ToString(), *Reason.ToString());
}

void USeinWorldSubsystem::TickMatchState()
{
	if (MatchState == ESeinMatchState::Starting)
	{
		if (CurrentTick >= StartingStateDeadlineTick)
		{
			MatchState = ESeinMatchState::Playing;
			MatchStartTick = CurrentTick;
			EnqueueVisualEvent(FSeinVisualEvent::MakeMatchFlowEvent(ESeinVisualEventType::MatchStarted));
			UE_LOG(LogSeinSim, Log, TEXT("Match transitioned to Playing at tick %d"), CurrentTick);
		}
	}
}

// ==================== Voting (DESIGN §18) ====================

void USeinWorldSubsystem::StartVote(FGameplayTag VoteType, ESeinVoteResolution Resolution, int32 RequiredThreshold, int32 ExpiresInTicks, FSeinPlayerID Initiator)
{
	if (!VoteType.IsValid()) return;
	if (ActiveVotes.Contains(VoteType))
	{
		UE_LOG(LogSeinSim, Warning, TEXT("StartVote: vote %s already active"), *VoteType.ToString());
		return;
	}
	FSeinVoteState Vote;
	Vote.VoteType = VoteType;
	Vote.Resolution = Resolution;
	Vote.RequiredThreshold = FMath::Max(1, RequiredThreshold);
	Vote.InitiatedAtTick = CurrentTick;
	Vote.ExpiresAtTick = (ExpiresInTicks > 0) ? CurrentTick + ExpiresInTicks : INT32_MAX;
	Vote.Initiator = Initiator;
	ActiveVotes.Add(VoteType, MoveTemp(Vote));

	EnqueueVisualEvent(FSeinVisualEvent::MakeVoteStartedEvent(VoteType, Initiator, RequiredThreshold));
}

void USeinWorldSubsystem::CastVote(FGameplayTag VoteType, FSeinPlayerID Voter, int32 VoteValue)
{
	if (!VoteType.IsValid()) return;
	FSeinVoteState* Vote = ActiveVotes.Find(VoteType);
	if (!Vote) return;
	Vote->Votes.Add(Voter, VoteValue);

	int32 Yes = 0, No = 0;
	for (const auto& Pair : Vote->Votes) { (Pair.Value > 0) ? ++Yes : ++No; }
	EnqueueVisualEvent(FSeinVisualEvent::MakeVoteProgressEvent(VoteType, Yes, No));

	EvaluateAndResolveVote(VoteType);
}

ESeinVoteStatus USeinWorldSubsystem::GetVoteStatus(FGameplayTag VoteType) const
{
	if (!VoteType.IsValid()) return ESeinVoteStatus::NotStarted;
	return ActiveVotes.Contains(VoteType) ? ESeinVoteStatus::Active : ESeinVoteStatus::NotStarted;
}

TArray<FSeinVoteState> USeinWorldSubsystem::GetActiveVotes() const
{
	TArray<FSeinVoteState> Out;
	Out.Reserve(ActiveVotes.Num());
	for (const auto& Pair : ActiveVotes) { Out.Add(Pair.Value); }
	return Out;
}

bool USeinWorldSubsystem::EvaluateAndResolveVote(FGameplayTag VoteType)
{
	FSeinVoteState* Vote = ActiveVotes.Find(VoteType);
	if (!Vote) return false;

	int32 Yes = 0, No = 0;
	for (const auto& Pair : Vote->Votes) { (Pair.Value > 0) ? ++Yes : ++No; }

	// Eligible voter count — V1 uses the live registered-player count (including
	// Neutral). More precise predicates (exclude spectators, exclude AI, etc.)
	// land when the match-settings-driven vote-eligibility policy does.
	const int32 Eligible = FMath::Max(1, PlayerStates.Num());

	bool bPassed = false;
	bool bResolveNow = false;
	switch (Vote->Resolution)
	{
	case ESeinVoteResolution::Majority:
		if (Yes * 2 > Eligible) { bPassed = true; bResolveNow = true; }
		else if (Yes + No >= Eligible) { bResolveNow = true; bPassed = false; }
		break;
	case ESeinVoteResolution::Unanimous:
		if (Yes >= Eligible) { bPassed = true; bResolveNow = true; }
		else if (No > 0) { bResolveNow = true; bPassed = false; }
		break;
	case ESeinVoteResolution::HostDecides:
		// V1: any "yes" passes, any "no" fails. Host designation lands with
		// §18 match-flow network plumbing; until then treat first vote as decisive.
		if (Yes > 0) { bPassed = true; bResolveNow = true; }
		else if (No > 0) { bResolveNow = true; bPassed = false; }
		break;
	case ESeinVoteResolution::Plurality:
		if (Yes + No >= Eligible)
		{
			bResolveNow = true;
			bPassed = (Yes > No);
		}
		break;
	}

	// Also check the explicit threshold (overrides resolution if passed first).
	if (!bResolveNow && Yes >= Vote->RequiredThreshold)
	{
		bResolveNow = true;
		bPassed = true;
	}

	if (bResolveNow)
	{
		const FGameplayTag Resolved = Vote->VoteType;
		EnqueueVisualEvent(FSeinVisualEvent::MakeVoteResolvedEvent(Resolved, bPassed));
		ActiveVotes.Remove(VoteType);
		return true;
	}
	return false;
}

void USeinWorldSubsystem::TickVotes()
{
	if (ActiveVotes.Num() == 0) return;
	TArray<FGameplayTag> Expired;
	for (const auto& Pair : ActiveVotes)
	{
		if (CurrentTick >= Pair.Value.ExpiresAtTick) { Expired.Add(Pair.Key); }
	}
	for (const FGameplayTag& Tag : Expired)
	{
		// Expired votes that haven't passed on their own fail deterministically.
		EnqueueVisualEvent(FSeinVisualEvent::MakeVoteResolvedEvent(Tag, /*bPassed=*/false));
		ActiveVotes.Remove(Tag);
	}
}

// ==================== Diplomacy (DESIGN §18) ====================

void USeinWorldSubsystem::ApplyDiplomacyDelta(FSeinPlayerID FromPlayer, FSeinPlayerID ToPlayer, const FGameplayTagContainer& TagsToAdd, const FGameplayTagContainer& TagsToRemove)
{
	const FSeinDiplomacyKey Key(FromPlayer, ToPlayer);
	FGameplayTagContainer& Current = DiplomacyRelations.FindOrAdd(Key);

	// Remove first so the index-update sequence is consistent even when the
	// same tag appears in both TagsToAdd + TagsToRemove.
	for (const FGameplayTag& Tag : TagsToRemove)
	{
		if (Current.RemoveTag(Tag))
		{
			if (FSeinDiplomacyIndexBucket* Bucket = DiplomacyTagIndex.Find(Tag))
			{
				Bucket->Pairs.RemoveSingle(Key);
				if (Bucket->Pairs.Num() == 0) { DiplomacyTagIndex.Remove(Tag); }
			}
		}
	}
	for (const FGameplayTag& Tag : TagsToAdd)
	{
		if (!Current.HasTagExact(Tag))
		{
			Current.AddTag(Tag);
			DiplomacyTagIndex.FindOrAdd(Tag).Pairs.AddUnique(Key);
		}
	}

	// Emit one event per delta — representative tag = first added (or first
	// removed if no adds). UI queries `GetDiplomacyTags` for authoritative post-change state.
	FGameplayTag Representative;
	if (TagsToAdd.Num() > 0) { Representative = TagsToAdd.First(); }
	else if (TagsToRemove.Num() > 0) { Representative = TagsToRemove.First(); }
	EnqueueVisualEvent(FSeinVisualEvent::MakeDiplomacyChangedEvent(FromPlayer, ToPlayer, Representative));
}

FGameplayTagContainer USeinWorldSubsystem::GetDiplomacyTags(FSeinPlayerID FromPlayer, FSeinPlayerID ToPlayer) const
{
	const FSeinDiplomacyKey Key(FromPlayer, ToPlayer);
	if (const FGameplayTagContainer* Found = DiplomacyRelations.Find(Key))
	{
		return *Found;
	}
	return FGameplayTagContainer{};
}

TArray<FSeinDiplomacyKey> USeinWorldSubsystem::GetPairsWithDiplomacyTag(FGameplayTag Tag) const
{
	if (const FSeinDiplomacyIndexBucket* Bucket = DiplomacyTagIndex.Find(Tag))
	{
		return Bucket->Pairs;
	}
	return {};
}

// ==================== AI (DESIGN §16) ====================

void USeinWorldSubsystem::RegisterAIController(USeinAIController* Controller, FSeinPlayerID OwnedPlayer)
{
	if (!Controller) return;
	// Idempotent — if already registered, reseat the owned player + subsystem.
	if (!AIControllers.Contains(Controller))
	{
		AIControllers.Add(Controller);
	}
	Controller->OwnedPlayerID = OwnedPlayer;
	Controller->WorldSubsystem = this;
	Controller->OnRegistered();
	UE_LOG(LogSeinSim, Log, TEXT("Registered AI controller %s for player %s"),
		*Controller->GetName(), *OwnedPlayer.ToString());
}

void USeinWorldSubsystem::UnregisterAIController(USeinAIController* Controller)
{
	if (!Controller) return;
	const int32 Removed = AIControllers.Remove(Controller);
	if (Removed > 0)
	{
		Controller->OnUnregistered();
		Controller->WorldSubsystem = nullptr;
	}
}

USeinAIController* USeinWorldSubsystem::GetAIControllerForPlayer(FSeinPlayerID PlayerID) const
{
	for (const TObjectPtr<USeinAIController>& Ctrl : AIControllers)
	{
		if (Ctrl && Ctrl->OwnedPlayerID == PlayerID)
		{
			return Ctrl;
		}
	}
	return nullptr;
}

void USeinWorldSubsystem::TickAIControllers(FFixedPoint DeltaTime)
{
	// Snapshot the list so Tick callbacks that register/unregister don't crash
	// the iteration; pending removals take effect next tick.
	TArray<TObjectPtr<USeinAIController>> Snapshot = AIControllers;

	// DETERMINISM: sort by OwnedPlayerID before ticking. Registration order depends
	// on actor spawn order + BeginPlay sequencing, which can differ across clients.
	// PlayerIDs are globally agreed (registered via RegisterPlayer); sorting by
	// PlayerID.Value pins tick order network-wide. If two controllers somehow share
	// a PlayerID, we fall back to pointer-index stability — indeterminate but rare
	// (would be a misconfiguration; log as warning).
	Snapshot.StableSort([](const TObjectPtr<USeinAIController>& A, const TObjectPtr<USeinAIController>& B)
	{
		if (!A) return false;
		if (!B) return true;
		return A->OwnedPlayerID < B->OwnedPlayerID;
	});

	for (const TObjectPtr<USeinAIController>& Ctrl : Snapshot)
	{
		if (!Ctrl) continue;
		FSeinAITickContext Ctx;
		Ctx.CurrentTick = CurrentTick;
		Ctx.DeltaTime = DeltaTime;
		Ctx.OwnedPlayerID = Ctrl->OwnedPlayerID;
		Ctrl->Tick(Ctx);
	}
}

// ==================== Relationships (DESIGN §14) ====================

namespace
{
	// Assign the first invalid/free visual-slot index in a container's
	// TotalCapacity-sized VisualSlotAssignments array. Growing the array lazily
	// keeps cost proportional to actual occupant count; TotalCapacity just caps
	// the search. Returns INDEX_NONE if every slot is filled.
	int32 AssignFirstFreeVisualSlot(FSeinContainmentData& Container, FSeinEntityHandle Occupant)
	{
		if (!Container.bTracksVisualSlots) return INDEX_NONE;
		if (Container.VisualSlotAssignments.Num() < Container.TotalCapacity)
		{
			Container.VisualSlotAssignments.SetNum(Container.TotalCapacity);
		}
		for (int32 i = 0; i < Container.VisualSlotAssignments.Num(); ++i)
		{
			if (!Container.VisualSlotAssignments[i].IsValid())
			{
				Container.VisualSlotAssignments[i] = Occupant;
				return i;
			}
		}
		return INDEX_NONE;
	}
}

bool USeinWorldSubsystem::EnterContainer(FSeinEntityHandle Entity, FSeinEntityHandle Container)
{
	if (!EntityPool.IsValid(Entity) || !EntityPool.IsValid(Container))
	{
		return false;
	}
	if (Entity == Container)
	{
		UE_LOG(LogSeinSim, Warning, TEXT("EnterContainer: entity %s cannot enter itself"), *Entity.ToString());
		return false;
	}

	FSeinContainmentMemberData* MemComp = GetComponent<FSeinContainmentMemberData>(Entity);
	if (!MemComp)
	{
		UE_LOG(LogSeinSim, Warning, TEXT("EnterContainer: entity %s has no FSeinContainmentMemberData"), *Entity.ToString());
		return false;
	}
	if (MemComp->CurrentContainer.IsValid())
	{
		UE_LOG(LogSeinSim, Warning, TEXT("EnterContainer: entity %s already contained by %s"),
			*Entity.ToString(), *MemComp->CurrentContainer.ToString());
		return false;
	}

	FSeinContainmentData* ContComp = GetComponent<FSeinContainmentData>(Container);
	if (!ContComp)
	{
		UE_LOG(LogSeinSim, Warning, TEXT("EnterContainer: container %s has no FSeinContainmentData"), *Container.ToString());
		return false;
	}

	// Capacity
	if (ContComp->CurrentLoad + MemComp->Size > ContComp->TotalCapacity)
	{
		return false;
	}

	// Tag query (empty query = permissive)
	if (!ContComp->AcceptedEntityQuery.IsEmpty())
	{
		const FSeinTagData* TagComp = GetComponent<FSeinTagData>(Entity);
		const FGameplayTagContainer EntityTags = TagComp ? TagComp->CombinedTags : FGameplayTagContainer{};
		if (!ContComp->AcceptedEntityQuery.Matches(EntityTags))
		{
			return false;
		}
	}

	// Commit state
	ContComp->Occupants.Add(Entity);
	ContComp->CurrentLoad += MemComp->Size;
	MemComp->CurrentContainer = Container;
	MemComp->VisualSlotIndex = AssignFirstFreeVisualSlot(*ContComp, Entity);
	// CurrentSlot stays empty — set by AttachToSlot when attachment is used.

	// Visibility-mode spatial effect: Hidden + Partial remove from grid; only
	// PositionedRelative stays registered (rendered via container + offset).
	if (ContComp->Visibility != ESeinContainmentVisibility::PositionedRelative)
	{
		if (SpatialGridUnregisterCallback.IsBound())
		{
			SpatialGridUnregisterCallback.Execute(Entity);
		}
	}

	EnqueueVisualEvent(FSeinVisualEvent::MakeEntityEnteredContainerEvent(
		Container, Entity, MemComp->VisualSlotIndex));
	return true;
}

bool USeinWorldSubsystem::ExitContainer(FSeinEntityHandle Entity, FFixedVector ExitLocation)
{
	if (!EntityPool.IsValid(Entity)) return false;

	FSeinContainmentMemberData* MemComp = GetComponent<FSeinContainmentMemberData>(Entity);
	if (!MemComp || !MemComp->CurrentContainer.IsValid())
	{
		return false;
	}
	const FSeinEntityHandle Container = MemComp->CurrentContainer;
	if (!EntityPool.IsValid(Container))
	{
		// Stale pointer — scrub and bail.
		MemComp->CurrentContainer = FSeinEntityHandle();
		MemComp->CurrentSlot = FGameplayTag();
		MemComp->VisualSlotIndex = INDEX_NONE;
		return false;
	}

	FSeinContainmentData* ContComp = GetComponent<FSeinContainmentData>(Container);
	if (!ContComp) return false;

	// Resolve exit world position.
	FFixedVector FinalExit = ExitLocation;
	if (FinalExit == FFixedVector())
	{
		FFixedVector ContainerLoc;
		if (const FSeinEntity* ContEntity = GetEntity(Container))
		{
			ContainerLoc = ContEntity->Transform.GetLocation();
		}
		FinalExit = ContainerLoc;
		if (const FSeinTransportSpec* TransportSpec = GetComponent<FSeinTransportSpec>(Container))
		{
			FinalExit = ContainerLoc + TransportSpec->DeployOffset;
		}
	}

	// Write the exiter's transform.
	if (FSeinEntity* ExiterEntity = EntityPool.Get(Entity))
	{
		FFixedTransform NewXfm = ExiterEntity->Transform;
		NewXfm.SetLocation(FinalExit);
		ExiterEntity->Transform = NewXfm;
	}

	// Attachment-slot cleanup, if any.
	if (MemComp->CurrentSlot.IsValid())
	{
		if (FSeinAttachmentSpec* Spec = GetComponent<FSeinAttachmentSpec>(Container))
		{
			Spec->Assignments.Remove(MemComp->CurrentSlot);
		}
		EnqueueVisualEvent(FSeinVisualEvent::MakeAttachmentSlotEmptiedEvent(
			Container, Entity, MemComp->CurrentSlot));
	}

	// Visual-slot cleanup.
	if (ContComp->bTracksVisualSlots && ContComp->VisualSlotAssignments.IsValidIndex(MemComp->VisualSlotIndex))
	{
		ContComp->VisualSlotAssignments[MemComp->VisualSlotIndex] = FSeinEntityHandle();
	}

	// Occupant-list cleanup.
	ContComp->Occupants.Remove(Entity);
	ContComp->CurrentLoad = FMath::Max(0, ContComp->CurrentLoad - MemComp->Size);
	MemComp->CurrentContainer = FSeinEntityHandle();
	MemComp->CurrentSlot = FGameplayTag();
	MemComp->VisualSlotIndex = INDEX_NONE;

	// Re-register in spatial grid if the container was Hidden/Partial.
	if (ContComp->Visibility != ESeinContainmentVisibility::PositionedRelative)
	{
		if (SpatialGridRegisterCallback.IsBound())
		{
			SpatialGridRegisterCallback.Execute(Entity);
		}
	}

	EnqueueVisualEvent(FSeinVisualEvent::MakeEntityExitedContainerEvent(Container, Entity, FinalExit));
	return true;
}

bool USeinWorldSubsystem::AttachToSlot(FSeinEntityHandle Entity, FSeinEntityHandle Container, FGameplayTag SlotTag)
{
	if (!EntityPool.IsValid(Entity) || !EntityPool.IsValid(Container)) return false;
	if (!SlotTag.IsValid()) return false;

	FSeinAttachmentSpec* Spec = GetComponent<FSeinAttachmentSpec>(Container);
	if (!Spec) return false;

	// Locate slot by tag.
	const FSeinAttachmentSlotDef* SlotDef = Spec->Slots.FindByPredicate(
		[&](const FSeinAttachmentSlotDef& S) { return S.SlotTag == SlotTag; });
	if (!SlotDef) return false;

	// Already filled?
	if (FSeinEntityHandle* Existing = Spec->Assignments.Find(SlotTag))
	{
		if (Existing->IsValid()) return false;
	}

	// Slot-level tag query (independent of container-level AcceptedEntityQuery).
	if (!SlotDef->AcceptedEntityQuery.IsEmpty())
	{
		const FSeinTagData* TagComp = GetComponent<FSeinTagData>(Entity);
		const FGameplayTagContainer EntityTags = TagComp ? TagComp->CombinedTags : FGameplayTagContainer{};
		if (!SlotDef->AcceptedEntityQuery.Matches(EntityTags))
		{
			return false;
		}
	}

	// Attachment implies containment — run the standard enter path first.
	if (!EnterContainer(Entity, Container))
	{
		return false;
	}

	// Stamp slot assignment + member back-ref.
	Spec->Assignments.Add(SlotTag, Entity);
	if (FSeinContainmentMemberData* Mem = GetComponent<FSeinContainmentMemberData>(Entity))
	{
		Mem->CurrentSlot = SlotTag;
	}

	EnqueueVisualEvent(FSeinVisualEvent::MakeAttachmentSlotFilledEvent(Container, Entity, SlotTag));
	return true;
}

bool USeinWorldSubsystem::DetachFromSlot(FSeinEntityHandle Entity)
{
	if (!EntityPool.IsValid(Entity)) return false;
	FSeinContainmentMemberData* Mem = GetComponent<FSeinContainmentMemberData>(Entity);
	if (!Mem || !Mem->CurrentSlot.IsValid()) return false;
	// ExitContainer handles slot-assignment removal + visual event; simply delegate.
	return ExitContainer(Entity);
}

void USeinWorldSubsystem::PropagateContainerDeath(FSeinEntityHandle DyingContainer)
{
	FSeinContainmentData* Container = GetComponent<FSeinContainmentData>(DyingContainer);
	if (!Container) return;

	FFixedVector ContainerLoc;
	if (const FSeinEntity* ContEntity = GetEntity(DyingContainer))
	{
		ContainerLoc = ContEntity->Transform.GetLocation();
	}

	const bool bEject = Container->bEjectOnContainerDeath;
	const TSubclassOf<USeinEffect> OnEjectEffect = Container->OnEjectEffect;
	const TSubclassOf<USeinEffect> OnDeathEffect = Container->OnContainerDeathEffect;

	// Snapshot — occupants list is mutated while iterating when ExitContainer
	// runs, so copy first.
	TArray<FSeinEntityHandle> Occupants = Container->Occupants;
	for (const FSeinEntityHandle& Occ : Occupants)
	{
		if (!EntityPool.IsValid(Occ)) continue;

		if (bEject)
		{
			// Exit at container's last location; apply eject effect if authored.
			ExitContainer(Occ, ContainerLoc);
			if (OnEjectEffect)
			{
				ApplyEffect(Occ, OnEjectEffect, DyingContainer);
			}
		}
		else
		{
			// Occupant dies with container; optional effect first, then destroy.
			if (OnDeathEffect)
			{
				ApplyEffect(Occ, OnDeathEffect, DyingContainer);
			}
			DestroyEntity(Occ);
		}
	}

	// Container's Occupants now empty; its FSeinContainmentData is about to be
	// stripped by the surrounding ProcessDeferredDestroys sweep.
}

FSeinEntityHandle USeinWorldSubsystem::GetImmediateContainer(FSeinEntityHandle Entity) const
{
	const FSeinContainmentMemberData* Mem = GetComponent<FSeinContainmentMemberData>(Entity);
	return (Mem && EntityPool.IsValid(Mem->CurrentContainer)) ? Mem->CurrentContainer : FSeinEntityHandle();
}

FSeinEntityHandle USeinWorldSubsystem::GetRootContainer(FSeinEntityHandle Entity) const
{
	FSeinEntityHandle Cursor = GetImmediateContainer(Entity);
	if (!Cursor.IsValid()) return FSeinEntityHandle();
	// Walk up; cap at 32 to guard against pathological loops.
	for (int32 Depth = 0; Depth < 32; ++Depth)
	{
		const FSeinEntityHandle Next = GetImmediateContainer(Cursor);
		if (!Next.IsValid()) return Cursor;
		Cursor = Next;
	}
	UE_LOG(LogSeinSim, Warning, TEXT("GetRootContainer: depth limit hit on %s — likely a containment cycle"),
		*Entity.ToString());
	return Cursor;
}

bool USeinWorldSubsystem::IsContained(FSeinEntityHandle Entity) const
{
	const FSeinContainmentMemberData* Mem = GetComponent<FSeinContainmentMemberData>(Entity);
	return Mem && EntityPool.IsValid(Mem->CurrentContainer);
}

TArray<FSeinEntityHandle> USeinWorldSubsystem::GetAllNestedOccupants(FSeinEntityHandle Container) const
{
	TArray<FSeinEntityHandle> Out;
	const FSeinContainmentData* Cont = GetComponent<FSeinContainmentData>(Container);
	if (!Cont) return Out;

	TArray<FSeinEntityHandle> Frontier = Cont->Occupants;
	while (Frontier.Num() > 0)
	{
		FSeinEntityHandle Current = Frontier.Pop();
		if (!EntityPool.IsValid(Current)) continue;
		Out.Add(Current);
		if (const FSeinContainmentData* Nested = GetComponent<FSeinContainmentData>(Current))
		{
			Frontier.Append(Nested->Occupants);
		}
	}
	return Out;
}

FSeinContainmentTree USeinWorldSubsystem::BuildContainmentTree(FSeinEntityHandle Container) const
{
	FSeinContainmentTree Tree;

	// Iterative DFS emitting entries in pre-order so the flattened array encodes
	// the hierarchy: each child appears after its parent and is flagged with
	// Depth + ParentIndex. BP consumers walk sequentially to rebuild the tree.
	struct FFrame { FSeinEntityHandle Entity; int32 Depth; int32 ParentIndex; };
	TArray<FFrame> Stack;
	Stack.Reserve(8);
	Stack.Push({Container, 0, INDEX_NONE});

	while (Stack.Num() > 0)
	{
		const FFrame Frame = Stack.Pop();
		if (!EntityPool.IsValid(Frame.Entity)) continue;

		FSeinContainmentTreeEntry Entry;
		Entry.Entity = Frame.Entity;
		Entry.Depth = Frame.Depth;
		Entry.ParentIndex = Frame.ParentIndex;
		const int32 ThisIndex = Tree.Entries.Add(Entry);

		if (const FSeinContainmentData* Cont = GetComponent<FSeinContainmentData>(Frame.Entity))
		{
			// Push children in reverse order so stack-popped order matches original
			// occupant list order (deterministic).
			for (int32 i = Cont->Occupants.Num() - 1; i >= 0; --i)
			{
				Stack.Push({Cont->Occupants[i], Frame.Depth + 1, ThisIndex});
			}
		}
	}

	return Tree;
}
