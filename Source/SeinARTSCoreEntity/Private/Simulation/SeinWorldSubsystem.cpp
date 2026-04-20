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
				UE_LOG(LogSeinSim, Warning, TEXT("ActivateAbility[%s]: entity %s has no FSeinAbilityData"),
					*Cmd.AbilityTag.ToString(), *Cmd.EntityHandle.ToString());
				break;
			}

			USeinAbility* Ability = AbilityComp->FindAbilityByTag(Cmd.AbilityTag);
			if (!Ability)
			{
				UE_LOG(LogSeinSim, Warning, TEXT("ActivateAbility[%s]: entity %s has no ability with that tag (%d instances from %d granted classes)"),
					*Cmd.AbilityTag.ToString(), *Cmd.EntityHandle.ToString(),
					AbilityComp->AbilityInstances.Num(), AbilityComp->GrantedAbilityClasses.Num());
				break;
			}
			if (Ability->IsOnCooldown())
			{
				UE_LOG(LogSeinSim, Warning, TEXT("ActivateAbility[%s]: on cooldown"), *Cmd.AbilityTag.ToString());
				break;
			}

			// Tag-based activation arbitration.
			//
			// 1) ActivationBlockedTags: if the entity carries any of these tags,
			//    refuse the activation. Combined with an ability owning the same
			//    tag it blocks on, this gives self-block (e.g., grenade throw
			//    can't reissue while channeling).
			//
			// 2) CancelAbilitiesWithTag: for every active ability on the entity
			//    (including this one), if its OwnedTags intersect this ability's
			//    cancel set, cancel it. An ability that lists one of its own
			//    OwnedTags in CancelAbilitiesWithTag gets self-cancelling reissue
			//    (e.g., right-click-spam Move).
			//
			// 3) Activate. USeinAbility::ActivateAbility applies OwnedTags (diffed
			//    against tags already present) and fires OnActivate.
			if (!Ability->ActivationBlockedTags.IsEmpty())
			{
				const FSeinTagData* TagComp = GetComponent<FSeinTagData>(Cmd.EntityHandle);
				if (TagComp && TagComp->CombinedTags.HasAny(Ability->ActivationBlockedTags))
				{
					UE_LOG(LogSeinSim, Warning, TEXT("ActivateAbility[%s]: blocked by entity tags"),
						*Cmd.AbilityTag.ToString());
					break;
				}
			}

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
	// Get base value from component
	ISeinComponentStorage* Storage = GetComponentStorageRaw(ComponentType);
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
