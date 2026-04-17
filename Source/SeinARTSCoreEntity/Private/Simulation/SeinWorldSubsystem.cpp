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
#include "Abilities/SeinLatentActionManager.h"
#include "Components/SeinAbilityData.h"
#include "Components/SeinActiveEffectsData.h"
#include "Components/SeinProductionData.h"
#include "Components/SeinTagData.h"
#include "Components/ActorComponents/SeinActorComponent.h"
#include "Attributes/SeinModifier.h"
#include "Attributes/SeinAttributeResolver.h"
#include "Containers/Ticker.h"
#include "StructUtils/InstancedStruct.h"

// Built-in systems
#include "Simulation/Systems/SeinEffectTickSystem.h"
#include "Simulation/Systems/SeinCooldownSystem.h"
#include "Simulation/Systems/SeinAbilityTickSystem.h"
#include "Simulation/Systems/SeinProductionSystem.h"
#include "Simulation/Systems/SeinResourceSystem.h"
#include "Simulation/Systems/SeinStateHashSystem.h"

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
	BuiltInSystems.Add(new FSeinStateHashSystem());

	for (ISeinSystem* Sys : BuiltInSystems)
	{
		RegisterSystem(Sys);
	}

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
		// Observer commands (CameraUpdate, SelectionChanged) are logged for replays
		// but never processed by the sim.
		if (Cmd.IsObserverCommand())
		{
			continue;
		}

		// Ping commands don't require a valid entity — emit a visual event and continue
		if (Cmd.CommandType == ESeinCommandType::Ping)
		{
			EnqueueVisualEvent(FSeinVisualEvent::MakePingEvent(
				Cmd.PlayerID, Cmd.TargetLocation, Cmd.TargetEntity));
			continue;
		}

		if (!EntityPool.IsValid(Cmd.EntityHandle))
		{
			continue;
		}

		switch (Cmd.CommandType)
		{
		case ESeinCommandType::ActivateAbility:
		{
			FSeinAbilityData* AbilityComp = GetComponent<FSeinAbilityData>(Cmd.EntityHandle);
			if (!AbilityComp)
			{
				UE_LOG(LogSeinSim, Warning, TEXT("ActivateAbility[%s]: entity %s has no SeinAbilityComponent"),
					*Cmd.AbilityTag.ToString(), *Cmd.EntityHandle.ToString());
				break;
			}

			USeinAbility* Ability = AbilityComp->FindAbilityByTag(Cmd.AbilityTag);
			if (!Ability)
			{
				UE_LOG(LogSeinSim, Warning, TEXT("ActivateAbility[%s]: entity %s has AbilityComponent but no ability with that tag (has %d instances from %d granted classes)"),
					*Cmd.AbilityTag.ToString(), *Cmd.EntityHandle.ToString(),
					AbilityComp->AbilityInstances.Num(), AbilityComp->GrantedAbilityClasses.Num());
				break;
			}
			if (Ability->IsOnCooldown())
			{
				UE_LOG(LogSeinSim, Warning, TEXT("ActivateAbility[%s]: on cooldown"), *Cmd.AbilityTag.ToString());
				break;
			}
			if (Ability->bIsActive)
			{
				UE_LOG(LogSeinSim, Warning, TEXT("ActivateAbility[%s]: already active"), *Cmd.AbilityTag.ToString());
				break;
			}

			// Cancel current active ability if needed
			if (AbilityComp->ActiveAbility && AbilityComp->ActiveAbility->bIsActive)
			{
				AbilityComp->ActiveAbility->CancelAbility();
			}

			Ability->ActivateAbility(Cmd.TargetEntity, Cmd.TargetLocation);
			if (!Ability->bIsPassive)
			{
				AbilityComp->ActiveAbility = Ability;
			}
			break;
		}

		case ESeinCommandType::CancelAbility:
		{
			FSeinAbilityData* AbilityComp = GetComponent<FSeinAbilityData>(Cmd.EntityHandle);
			if (AbilityComp && AbilityComp->ActiveAbility && AbilityComp->ActiveAbility->bIsActive)
			{
				AbilityComp->ActiveAbility->CancelAbility();
				AbilityComp->ActiveAbility = nullptr;
			}
			break;
		}

		case ESeinCommandType::QueueProduction:
		{
			FSeinProductionData* ProdComp = GetComponent<FSeinProductionData>(Cmd.EntityHandle);
			if (!ProdComp || !ProdComp->CanQueueMore()) break;

			// Find the producible class matching the command's AbilityTag vs CDO ArchetypeTag
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
			if (!MatchedClass) break;

			const USeinArchetypeDefinition* ProdArchetype = GetDefault<ASeinActor>(MatchedClass)->ArchetypeDefinition;

			// Check prerequisites
			FSeinPlayerState* PS = GetPlayerStateMutable(Cmd.PlayerID);
			if (!PS) break;
			if (!PS->HasAllTechTags(ProdArchetype->PrerequisiteTags)) break;

			// Check and deduct cost
			if (!PS->DeductResources(ProdArchetype->ProductionCost)) break;

			// Build queue entry
			FSeinProductionQueueEntry Entry;
			Entry.ActorClass = MatchedClass;
			Entry.TotalBuildTime = ProdArchetype->BuildTime;
			Entry.Cost = ProdArchetype->ProductionCost;
			Entry.bIsResearch = ProdArchetype->bIsResearch;
			if (ProdArchetype->bIsResearch)
			{
				Entry.ResearchTechTag = ProdArchetype->GrantedTechTag;
				Entry.ResearchModifiers = ProdArchetype->GrantedModifiers;
			}

			bool bWasEmpty = ProdComp->Queue.Num() == 0;
			ProdComp->Queue.Add(MoveTemp(Entry));

			if (bWasEmpty)
			{
				ProdComp->CurrentBuildProgress = FFixedPoint::Zero;
				EnqueueVisualEvent(FSeinVisualEvent::MakeProductionEvent(
					Cmd.EntityHandle, Cmd.AbilityTag, /*bCompleted=*/false));
			}
			break;
		}

		case ESeinCommandType::CancelProduction:
		{
			FSeinProductionData* ProdComp = GetComponent<FSeinProductionData>(Cmd.EntityHandle);
			if (!ProdComp) break;

			int32 CancelIdx = Cmd.QueueIndex;
			if (CancelIdx < 0 || CancelIdx >= ProdComp->Queue.Num()) break;

			// Refund the cost snapshot
			FSeinPlayerState* PS = GetPlayerStateMutable(Cmd.PlayerID);
			if (PS)
			{
				PS->AddResources(ProdComp->Queue[CancelIdx].Cost);
			}

			ProdComp->Queue.RemoveAt(CancelIdx);

			// If we cancelled the front item, reset progress
			if (CancelIdx == 0)
			{
				ProdComp->CurrentBuildProgress = FFixedPoint::Zero;
			}
			break;
		}

		case ESeinCommandType::SetRallyPoint:
		{
			FSeinProductionData* ProdComp = GetComponent<FSeinProductionData>(Cmd.EntityHandle);
			if (ProdComp)
			{
				ProdComp->RallyPoint = Cmd.TargetLocation;
			}
			break;
		}

		default:
			break;
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

	// Walk the CDO's USeinActorComponent subobjects and inject each one's sim
	// payload into deterministic storage. This replaces the legacy inline
	// ArchetypeDefinition.Components array — designers now author sim data via
	// typed wrapper ACs (Sein Movement, Sein Combat, ...) or BP subclasses of
	// USeinDynamicComponent.
	//
	// AActor::GetComponents() on a CDO returns only native CreateDefaultSubobject
	// components — BP-editor-added components live on the SCS, not the CDO. Use
	// the engine's CDO-inspection helper which walks both lists in a stable order.
	TArray<const USeinActorComponent*> SimACs;
	AActor::GetActorClassDefaultComponents<USeinActorComponent>(ActorClass, SimACs);
	for (const USeinActorComponent* AC : SimACs)
	{
		if (!AC) continue;
		const FInstancedStruct Payload = AC->Resolve();
		if (!Payload.IsValid()) continue;

		UScriptStruct* ComponentType = const_cast<UScriptStruct*>(Payload.GetScriptStruct());
		ISeinComponentStorageV2* Storage = GetOrCreateStorageForType(ComponentType);
		if (Storage)
		{
			Storage->AddComponent(Handle, Payload.GetMemory());
		}
	}

	// Initialize abilities if entity has an ability component
	InitializeEntityAbilities(Handle);

	// Auto-tag entity with its archetype tag (for archetype-scope modifier matching)
	if (ArchetypeDef->ArchetypeTag.IsValid())
	{
		FSeinTagData* TagComp = GetComponent<FSeinTagData>(Handle);
		if (TagComp)
		{
			TagComp->BaseTags.AddTag(ArchetypeDef->ArchetypeTag);
			TagComp->RebuildCombinedTags();
		}
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

		// Remove all components
		for (auto& Pair : ComponentStorages)
		{
			Pair.Value->RemoveAllForEntity(Handle);
		}

		// Fire destroy event
		EnqueueVisualEvent(FSeinVisualEvent::MakeDestroyEvent(Handle));

		// Remove from actor class map
		EntityActorClassMap.Remove(Handle);

		// Release slot back to pool
		EntityPool.Release(Handle);
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

	// Copy starting resources from faction if available
	if (TObjectPtr<USeinFaction>* FactionPtr = Factions.Find(FactionID))
	{
		NewState.Resources = (*FactionPtr)->StartingResources;
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

// ==================== Attribute Resolution ====================

FFixedPoint USeinWorldSubsystem::ResolveAttribute(FSeinEntityHandle Handle, UScriptStruct* ComponentType, FName FieldName)
{
	// Get base value from component
	ISeinComponentStorageV2* Storage = GetComponentStorageRaw(ComponentType);
	if (!Storage) return FFixedPoint::Zero;

	void* CompData = Storage->GetComponentRaw(Handle);
	if (!CompData) return FFixedPoint::Zero;

	FFixedPoint BaseValue = FSeinAttributeResolver::ReadFixedPointField(CompData, ComponentType, FieldName);

	// Collect modifiers
	TArray<FSeinModifier> AllModifiers;

	// Instance-level modifiers from active effects
	const FSeinActiveEffectsData* EffectsComp = GetComponent<FSeinActiveEffectsData>(Handle);
	if (EffectsComp)
	{
		for (const FSeinActiveEffect& Effect : EffectsComp->ActiveEffects)
		{
			for (const FSeinModifier& Mod : Effect.Definition.Modifiers)
			{
				if (Mod.Scope == ESeinModifierScope::Instance &&
					Mod.TargetComponentType == ComponentType &&
					Mod.TargetFieldName == FieldName)
				{
					AllModifiers.Add(Mod);
				}
			}
		}
	}

	// Archetype-level modifiers from player state (granted by completed research)
	FSeinPlayerID OwnerID = GetEntityOwner(Handle);
	if (const FSeinPlayerState* PlayerState = GetPlayerState(OwnerID))
	{
		const FSeinTagData* TagComp = GetComponent<FSeinTagData>(Handle);
		for (const FSeinModifier& Mod : PlayerState->ArchetypeModifiers)
		{
			if (Mod.Scope != ESeinModifierScope::Archetype) continue;
			if (Mod.TargetComponentType != ComponentType) continue;
			if (Mod.TargetFieldName != FieldName) continue;

			// If modifier targets a specific archetype tag, check the entity has it
			if (Mod.TargetArchetypeTag.IsValid())
			{
				if (!TagComp || !TagComp->CombinedTags.HasTag(Mod.TargetArchetypeTag))
				{
					continue;
				}
			}

			AllModifiers.Add(Mod);
		}
	}

	if (AllModifiers.Num() == 0)
	{
		return BaseValue;
	}

	return FSeinAttributeResolver::ResolveModifiers(BaseValue, AllModifiers);
}

// ==================== Component Storage Helpers ====================

ISeinComponentStorageV2* USeinWorldSubsystem::GetComponentStorageRaw(UScriptStruct* StructType)
{
	ISeinComponentStorageV2** Found = ComponentStorages.Find(StructType);
	return Found ? *Found : nullptr;
}

const ISeinComponentStorageV2* USeinWorldSubsystem::GetComponentStorageRaw(UScriptStruct* StructType) const
{
	ISeinComponentStorageV2* const* Found = ComponentStorages.Find(StructType);
	return Found ? *Found : nullptr;
}

ISeinComponentStorageV2* USeinWorldSubsystem::GetOrCreateStorageForType(UScriptStruct* StructType)
{
	if (ISeinComponentStorageV2** Found = ComponentStorages.Find(StructType))
	{
		return *Found;
	}

	// Create a generic byte-buffer storage for reflection-based component types.
	// This handles archetype-defined components that weren't pre-registered via RegisterComponentType<T>().
	FSeinGenericComponentStorageV2* Storage = new FSeinGenericComponentStorageV2(StructType, EntityPool.GetCapacity());
	ComponentStorages.Add(StructType, Storage);

	UE_LOG(LogSeinSim, Log, TEXT("Created generic storage for: %s"), *StructType->GetName());
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
		UE_LOG(LogSeinSim, Warning, TEXT("InitializeEntityAbilities: entity %s has no FSeinAbilityData"),
			*Handle.ToString());
		return;
	}

	UE_LOG(LogSeinSim, Warning, TEXT("InitializeEntityAbilities: entity %s  GrantedAbilityClasses=%d"),
		*Handle.ToString(), AbilityComp->GrantedAbilityClasses.Num());

	// Instantiate ability objects from class list
	for (const TSubclassOf<USeinAbility>& AbilityClass : AbilityComp->GrantedAbilityClasses)
	{
		if (!AbilityClass)
		{
			UE_LOG(LogSeinSim, Warning, TEXT("  - null ability class entry, skipping"));
			continue;
		}

		USeinAbility* AbilityInstance = NewObject<USeinAbility>(this, AbilityClass);
		AbilityInstance->InitializeAbility(Handle, this);
		AbilityComp->AbilityInstances.Add(AbilityInstance);

		UE_LOG(LogSeinSim, Warning, TEXT("  + instantiated %s  tag=%s  passive=%d"),
			*AbilityClass->GetName(),
			*AbilityInstance->AbilityTag.ToString(),
			AbilityInstance->bIsPassive ? 1 : 0);

		// Auto-activate passives
		if (AbilityInstance->bIsPassive)
		{
			AbilityInstance->ActivateAbility(Handle, FFixedVector::ZeroVector);
			AbilityComp->ActivePassives.Add(AbilityInstance);
		}
	}
}
