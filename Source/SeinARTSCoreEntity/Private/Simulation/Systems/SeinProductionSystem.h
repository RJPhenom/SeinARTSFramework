/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinProductionSystem.h
 * @brief   Advances production queues, spawns completed units, and fires visual events.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "Core/SeinPlayerState.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinProductionComponent.h"
#include "Events/SeinVisualEvent.h"
#include "Attributes/SeinModifier.h"

/**
 * System: Production
 * Phase: AbilityExecution | Priority: 50
 *
 * Iterates all entities with FSeinProductionComponent. For each entity
 * with a non-empty production queue:
 *  - Advances CurrentBuildProgress by DeltaTime.
 *  - When progress meets or exceeds TotalBuildTime, spawns the produced
 *    entity at the building's rally point (or offset from the building),
 *    fires a ProductionCompleted visual event, and dequeues the entry.
 *  - If the queue still has items, fires a ProductionStarted event.
 */
class FSeinProductionSystem final : public ISeinSystem
{
public:
	virtual void Tick(FFixedPoint DeltaTime, USeinWorldSubsystem& World) override
	{
		World.GetEntityPool().ForEachEntity([&](FSeinEntityHandle Handle, FSeinEntity& Entity)
		{
			FSeinProductionComponent* ProdComp = World.GetComponent<FSeinProductionComponent>(Handle);
			if (!ProdComp || ProdComp->Queue.Num() == 0)
			{
				return;
			}

			ProdComp->CurrentBuildProgress = ProdComp->CurrentBuildProgress + DeltaTime;

			const FSeinProductionQueueEntry& Front = ProdComp->Queue[0];

			if (ProdComp->CurrentBuildProgress >= Front.TotalBuildTime)
			{
				FSeinPlayerID OwnerID = World.GetEntityOwner(Handle);

				if (Front.bIsResearch)
				{
					// Research completion: grant tech tag and modifiers to player
					FSeinPlayerState* PS = World.GetPlayerStateMutable(OwnerID);
					if (PS)
					{
						PS->GrantTechTag(Front.ResearchTechTag);
						for (const FSeinModifier& Mod : Front.ResearchModifiers)
						{
							PS->AddArchetypeModifier(Mod);
						}
					}

					// Fire TechResearched visual event
					World.EnqueueVisualEvent(
						FSeinVisualEvent::MakeTechResearchedEvent(OwnerID, Front.ResearchTechTag));

					// Also fire ProductionCompleted for UI feedback
					World.EnqueueVisualEvent(
						FSeinVisualEvent::MakeProductionEvent(Handle, Front.ResearchTechTag, /*bCompleted=*/true));
				}
				else
				{
					TSubclassOf<ASeinActor> ActorClass = Front.ActorClass;
					if (ActorClass)
					{
						// Determine spawn location: use rally point if set, otherwise offset from building.
						FFixedVector SpawnLocation = ProdComp->RallyPoint;
						if (SpawnLocation == FFixedVector::ZeroVector)
						{
							// Default: offset 2 units in front of the building.
							SpawnLocation = Entity.Transform.GetLocation()
								+ FFixedVector(FFixedPoint::FromInt(2), FFixedPoint::Zero, FFixedPoint::Zero);
						}

						FFixedTransform SpawnTransform(SpawnLocation);

						World.SpawnEntity(ActorClass, SpawnTransform, OwnerID);

						// Fire ProductionCompleted visual event.
						World.EnqueueVisualEvent(
							FSeinVisualEvent::MakeProductionEvent(Handle, FGameplayTag(), /*bCompleted=*/true));
					}
				}

				// Dequeue the completed entry.
				ProdComp->Queue.RemoveAt(0);
				ProdComp->CurrentBuildProgress = FFixedPoint::Zero;

				// If more items remain, fire ProductionStarted for the next entry.
				if (ProdComp->Queue.Num() > 0)
				{
					World.EnqueueVisualEvent(
						FSeinVisualEvent::MakeProductionEvent(Handle, FGameplayTag(), /*bCompleted=*/false));
				}
			}
		});
	}

	virtual ESeinTickPhase GetPhase() const override { return ESeinTickPhase::AbilityExecution; }
	virtual int32 GetPriority() const override { return 50; }
	virtual FName GetSystemName() const override { return TEXT("Production"); }
};
