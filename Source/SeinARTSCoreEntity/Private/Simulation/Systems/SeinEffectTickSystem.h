/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinEffectTickSystem.h
 * @brief   Ticks active effects: decrements durations, fires periodic events,
 *          and cleans up expired effects including their granted tags.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinActiveEffectsData.h"
#include "Components/SeinTagData.h"
#include "Effects/SeinActiveEffect.h"
#include "Effects/SeinEffectDefinition.h"
#include "Events/SeinVisualEvent.h"

/**
 * System: Effect Tick
 * Phase: PreTick | Priority: 0
 *
 * Iterates all entities with FSeinActiveEffectsData and:
 *  - Decrements RemainingDuration on Timed effects; removes expired ones.
 *  - Tracks periodic timers; logs when a periodic tick fires.
 *  - On removal, strips granted tags from the entity's FSeinTagData.
 */
class FSeinEffectTickSystem final : public ISeinSystem
{
public:
	virtual void Tick(FFixedPoint DeltaTime, USeinWorldSubsystem& World) override
	{
		World.GetEntityPool().ForEachEntity([&](FSeinEntityHandle Handle, FSeinEntity& /*Entity*/)
		{
			FSeinActiveEffectsData* EffectsComp = World.GetComponent<FSeinActiveEffectsData>(Handle);
			if (!EffectsComp)
			{
				return;
			}

			TArray<uint32> ExpiredEffectIDs;

			for (FSeinActiveEffect& Effect : EffectsComp->ActiveEffects)
			{
				const FSeinEffectDefinition& Def = Effect.Definition;

				// --- Timed duration ---
				if (Def.DurationType == ESeinEffectDuration::Timed)
				{
					Effect.RemainingDuration = Effect.RemainingDuration - DeltaTime;

					if (Effect.RemainingDuration <= FFixedPoint::Zero)
					{
						ExpiredEffectIDs.Add(Effect.EffectInstanceID);
						continue;
					}
				}

				// --- Periodic tick ---
				if (Def.bPeriodic && Def.Period > FFixedPoint::Zero)
				{
					Effect.TimeSinceLastPeriodic = Effect.TimeSinceLastPeriodic + DeltaTime;

					if (Effect.TimeSinceLastPeriodic >= Def.Period)
					{
						Effect.TimeSinceLastPeriodic = Effect.TimeSinceLastPeriodic - Def.Period;

						// TODO: Fire periodic Blueprint event once the periodic delegate system is in place.
						UE_LOG(LogTemp, Verbose,
							TEXT("[EffectTick] Periodic tick for effect '%s' (ID %u) on entity %s"),
							*Def.EffectName.ToString(),
							Effect.EffectInstanceID,
							*Handle.ToString());
					}
				}
			}

			// --- Remove expired effects and clean up granted tags ---
			if (ExpiredEffectIDs.Num() > 0)
			{
				FSeinTagData* TagComp = World.GetComponent<FSeinTagData>(Handle);

				for (uint32 EffectID : ExpiredEffectIDs)
				{
					// Find the effect before removal so we can read its granted tags.
					const FSeinActiveEffect* EffectPtr = nullptr;
					for (const FSeinActiveEffect& E : EffectsComp->ActiveEffects)
					{
						if (E.EffectInstanceID == EffectID)
						{
							EffectPtr = &E;
							break;
						}
					}

					if (EffectPtr)
					{
						// Strip granted tags from the entity.
						if (TagComp)
						{
							for (const FGameplayTag& Tag : EffectPtr->Definition.GrantedTags)
							{
								TagComp->RemoveGrantedTag(Tag);
							}
						}

						// Fire visual event for effect removal.
						World.EnqueueVisualEvent(
							FSeinVisualEvent::MakeEffectEvent(Handle, EffectPtr->Definition.EffectTag, /*bApplied=*/false));
					}

					EffectsComp->RemoveEffect(EffectID);
				}
			}
		});
	}

	virtual ESeinTickPhase GetPhase() const override { return ESeinTickPhase::PreTick; }
	virtual int32 GetPriority() const override { return 0; }
	virtual FName GetSystemName() const override { return TEXT("EffectTick"); }
};
