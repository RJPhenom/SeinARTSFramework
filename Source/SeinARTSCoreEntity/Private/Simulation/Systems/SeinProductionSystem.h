/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinProductionSystem.h
 * @brief   Advances production queues. Handles the DESIGN §9 stall-at-completion
 *          semantics (front entry reaches 100% but waits for its AtCompletion
 *          cost to fit) and routes research completion through the unified
 *          effect apply pipeline per DESIGN §10.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "Core/SeinPlayerState.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinProductionData.h"
#include "Effects/SeinEffect.h"
#include "Events/SeinVisualEvent.h"
#include "Lib/SeinResourceBPFL.h"
#include "Lib/SeinStatsBPFL.h"
#include "Tags/SeinARTSGameplayTags.h"

/**
 * System: Production
 * Phase: AbilityExecution | Priority: 50
 *
 * Per tick for every entity with FSeinProductionData + non-empty Queue:
 *   1. Advance CurrentBuildProgress (unless already stalled — stall halts the timer).
 *   2. When progress >= TotalBuildTime, `AttemptSpawn`:
 *      a. If AtCompletionCost fits (catalog-aware CanAfford), deduct + spawn/apply.
 *      b. Else raise `bStalledAtCompletion`, fire ProductionStalled once, retry next tick.
 *   3. On successful completion: fire ProductionCompleted + (if research) TechResearched,
 *      dequeue, reset progress; fire ProductionStarted for the next entry if any.
 */
class FSeinProductionSystem final : public ISeinSystem
{
public:
	virtual void Tick(FFixedPoint DeltaTime, USeinWorldSubsystem& World) override
	{
		World.GetEntityPool().ForEachEntity([&](FSeinEntityHandle Handle, FSeinEntity& Entity)
		{
			FSeinProductionData* ProdComp = World.GetComponent<FSeinProductionData>(Handle);
			if (!ProdComp || ProdComp->Queue.Num() == 0) return;

			const FSeinPlayerID OwnerID = World.GetEntityOwner(Handle);

			// Advance progress unless we're already parked at 100% waiting on cap.
			if (!ProdComp->bStalledAtCompletion)
			{
				ProdComp->CurrentBuildProgress = ProdComp->CurrentBuildProgress + DeltaTime;
			}

			const FSeinProductionQueueEntry& Front = ProdComp->Queue[0];
			if (ProdComp->CurrentBuildProgress < Front.TotalBuildTime)
			{
				return;
			}

			// At or past 100%. Gate on AtCompletion affordability.
			if (!Front.AtCompletionCost.IsEmpty() &&
				!USeinResourceBPFL::SeinCanAfford(&World, OwnerID, Front.AtCompletionCost))
			{
				if (!ProdComp->bStalledAtCompletion)
				{
					ProdComp->bStalledAtCompletion = true;
					FGameplayTag ArchetypeTag;
					if (const ASeinActor* CDO = GetDefault<ASeinActor>(Front.ActorClass))
					{
						if (CDO->ArchetypeDefinition) ArchetypeTag = CDO->ArchetypeDefinition->ArchetypeTag;
					}
					World.EnqueueVisualEvent(FSeinVisualEvent::MakeProductionStalledEvent(Handle, ArchetypeTag));
				}
				return;
			}

			// AtCompletion deduct — catalog-aware (pop/supply resources add toward cap).
			if (!Front.AtCompletionCost.IsEmpty())
			{
				USeinResourceBPFL::SeinDeduct(&World, OwnerID, Front.AtCompletionCost);
			}

			FGameplayTag ProducedArchetypeTag;

			if (Front.bIsResearch)
			{
				// Unified tech path: apply the research effect class to the owner's
				// representative entity. The effect's scope routes modifiers + tags
				// to the right sim location (Instance / Archetype / Player).
				if (Front.ResearchEffectClass)
				{
					World.ApplyEffect(Handle, Front.ResearchEffectClass, Handle);
					if (const USeinEffect* EffectDef = GetDefault<USeinEffect>(Front.ResearchEffectClass))
					{
						ProducedArchetypeTag = EffectDef->EffectTag;
						if (ProducedArchetypeTag.IsValid())
						{
							World.EnqueueVisualEvent(
								FSeinVisualEvent::MakeTechResearchedEvent(OwnerID, ProducedArchetypeTag));
						}
					}
				}
			}
			else if (Front.ActorClass)
			{
				// Determine spawn location from rally target (entity resolves to current
				// transform; location falls back to building-offset if zero).
				FFixedVector SpawnLocation;
				if (ProdComp->RallyTarget.bIsEntityTarget && ProdComp->RallyTarget.EntityTarget.IsValid())
				{
					if (const FSeinEntity* RallyEntity = World.GetEntity(ProdComp->RallyTarget.EntityTarget))
					{
						SpawnLocation = RallyEntity->Transform.GetLocation();
					}
				}
				else
				{
					SpawnLocation = ProdComp->RallyTarget.Location;
				}
				if (SpawnLocation == FFixedVector::ZeroVector)
				{
					SpawnLocation = Entity.Transform.GetLocation()
						+ FFixedVector(FFixedPoint::FromInt(2), FFixedPoint::Zero, FFixedPoint::Zero);
				}

				World.SpawnEntity(Front.ActorClass, FFixedTransform(SpawnLocation), OwnerID);

				if (const ASeinActor* CDO = GetDefault<ASeinActor>(Front.ActorClass))
				{
					if (CDO->ArchetypeDefinition)
					{
						ProducedArchetypeTag = CDO->ArchetypeDefinition->ArchetypeTag;
					}
				}

				// Attribution — bump owning player's UnitsProduced counter (DESIGN §11).
				if (OwnerID.IsValid())
				{
					USeinStatsBPFL::SeinBumpStat(&World, OwnerID, SeinARTSTags::Stat_UnitsProduced, FFixedPoint::One);
				}

				// Rally auto-move: §5 CommandBroker integration pending (Session 4.1).
				// Pre-broker placeholder: produced units spawn at the rally target; no
				// broker-dispatched move command. When brokers land, issue an internal
				// Move dispatch here routed through the default CommandBroker resolver.
			}

			// Fire ProductionCompleted for UI. For research uses the effect tag; for
			// units uses the archetype tag. Empty tag is allowed if neither resolved.
			World.EnqueueVisualEvent(
				FSeinVisualEvent::MakeProductionEvent(Handle, ProducedArchetypeTag, /*bCompleted=*/true));

			// Dequeue + reset progress/stall.
			ProdComp->Queue.RemoveAt(0);
			ProdComp->CurrentBuildProgress = FFixedPoint::Zero;
			ProdComp->bStalledAtCompletion = false;

			// Fire ProductionStarted for the next entry, if any.
			if (ProdComp->Queue.Num() > 0)
			{
				FGameplayTag NextArchetypeTag;
				const FSeinProductionQueueEntry& Next = ProdComp->Queue[0];
				if (const ASeinActor* NextCDO = GetDefault<ASeinActor>(Next.ActorClass))
				{
					if (NextCDO->ArchetypeDefinition) NextArchetypeTag = NextCDO->ArchetypeDefinition->ArchetypeTag;
				}
				World.EnqueueVisualEvent(
					FSeinVisualEvent::MakeProductionEvent(Handle, NextArchetypeTag, /*bCompleted=*/false));
			}
		});
	}

	virtual ESeinTickPhase GetPhase() const override { return ESeinTickPhase::AbilityExecution; }
	virtual int32 GetPriority() const override { return 50; }
	virtual FName GetSystemName() const override { return TEXT("Production"); }
};
