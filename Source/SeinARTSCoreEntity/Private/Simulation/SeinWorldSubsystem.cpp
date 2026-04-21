/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWorldSubsystem.cpp
 * @brief   Implementation of the core simulation subsystem.
 */

#include "Simulation/SeinWorldSubsystem.h"
#include "Actor/SeinActor.h"
#include "Data/SeinArchetypeDefinition.h"
#include "Data/SeinFaction.h"
#include "Settings/PluginSettings.h"
#include "Core/SeinSimContext.h"
#include "Abilities/SeinAbility.h"
#include "Abilities/SeinAbilityValidation.h"
#include "Abilities/SeinLatentActionManager.h"
#include "Components/SeinAbilityData.h"
#include "Components/SeinActiveEffectsData.h"
#include "Components/SeinProductionData.h"
#include "Components/SeinTagData.h"
#include "Components/ActorComponents/SeinActorComponent.h"
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
#include "Simulation/Systems/SeinProductionSystem.h"
#include "Simulation/Systems/SeinResourceSystem.h"
#include "Simulation/Systems/SeinStateHashSystem.h"
#include "Simulation/Systems/SeinLifespanSystem.h"

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

	// Register built-in systems
	BuiltInSystems.Add(new FSeinEffectTickSystem());
	BuiltInSystems.Add(new FSeinCooldownSystem());
	BuiltInSystems.Add(new FSeinAbilityTickSystem());
	BuiltInSystems.Add(new FSeinProductionSystem());
	BuiltInSystems.Add(new FSeinResourceSystem());
	BuiltInSystems.Add(new FSeinLifespanSystem());
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

	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	const int32 MaxTicks = Settings->MaxTicksPerFrame;

	TimeAccumulator += DeltaTime;

	int32 TicksProcessed = 0;
	while (TimeAccumulator >= FixedDeltaTimeSeconds && TicksProcessed < MaxTicks)
	{
		CurrentTick++;
		TimeAccumulator -= FixedDeltaTimeSeconds;
		TicksProcessed++;

		// Convert to deterministic fixed-point (from compile-time constant, not runtime float)
		FFixedPoint SimDeltaTime = FFixedPoint::One / FFixedPoint::FromInt(Settings->SimulationTickRate);

		TickSystems(SimDeltaTime);
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

	// Phase 2: CommandProcessing — dequeue and dispatch commands
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

		// Ping commands don't require a valid entity — emit a visual event and continue.
		if (Cmd.CommandType == SeinARTSTags::Command_Type_Ping)
		{
			EnqueueVisualEvent(FSeinVisualEvent::MakePingEvent(
				Cmd.PlayerID, Cmd.TargetLocation, Cmd.TargetEntity));
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
				// OutOfRange + AutoMoveThen: pre-Phase 4 this falls through to Reject
				// (CommandBroker integration required — PLAN Session 4.1). Log the
				// intent so designers can tell the feature is stubbed, not ignored.
				if (ValidationResult == ESeinAbilityTargetValidationResult::OutOfRange &&
					Ability->OutOfRangeBehavior == ESeinOutOfRangeBehavior::AutoMoveThen)
				{
					UE_LOG(LogSeinSim, Warning,
						TEXT("ActivateAbility[%s]: AutoMoveThen requested but CommandBrokers not yet wired (Phase 4); rejecting"),
						*Cmd.AbilityTag.ToString());
				}
				else
				{
					UE_LOG(LogSeinSim, Warning, TEXT("ActivateAbility[%s]: target validation failed (%d)"),
						*Cmd.AbilityTag.ToString(), static_cast<int32>(ValidationResult));
				}
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

			// 3b. Pathable-target gate (DESIGN §13) — if enabled on this ability,
			// consult the nav-registered resolver for abstract-graph reachability
			// from the entity's current world position to the target position.
			if (Ability->bRequiresPathableTarget && PathableTargetResolver.IsBound())
			{
				const FSeinEntity* ActingEntity = GetEntity(Cmd.EntityHandle);
				if (ActingEntity)
				{
					const FVector FromWorld = ActingEntity->Transform.GetLocation().ToVector();
					const FVector ToWorld = Cmd.TargetLocation.ToVector();

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

			// 7. Cancel conflicting abilities. Self-cancelling reissue when an
			// ability lists one of its own OwnedTags here.
			if (!Ability->CancelAbilitiesWithTag.IsEmpty())
			{
				for (USeinAbility* Other : AbilityComp->AbilityInstances)
				{
					if (Other && Other->bIsActive &&
						Other->OwnedTags.HasAny(Ability->CancelAbilitiesWithTag))
					{
						Other->CancelAbility();
					}
				}
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
		const FInstancedStruct Payload = AC->Resolve();
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

int32 USeinWorldSubsystem::ComputeStateHash() const
{
	uint32 Hash = GetTypeHash(CurrentTick);

	// Hash all entities
	EntityPool.ForEachEntity([&Hash](FSeinEntityHandle Handle, const FSeinEntity& Entity)
	{
		Hash = HashCombine(Hash, GetTypeHash(Entity));
	});

	// Hash all component storages
	for (const auto& Pair : ComponentStorages)
	{
		Hash = HashCombine(Hash, Pair.Value->ComputeHash());
	}

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
