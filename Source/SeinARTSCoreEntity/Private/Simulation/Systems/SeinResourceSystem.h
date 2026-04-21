/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinResourceSystem.h
 * @brief   Applies passive resource income from entities to their owner players.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinResourceIncomeData.h"
#include "Core/SeinPlayerState.h"
#include "Data/SeinResourceTypes.h"

/**
 * System: Resource Income
 * Phase: PreTick | Priority: 20
 *
 * Iterates all entities with FSeinResourceIncomeData. For each entity,
 * computes IncomePerSecond * DeltaTime and adds the result to the owning
 * player's resource pool. Entities with no valid owner (neutral) are skipped.
 */
class FSeinResourceSystem final : public ISeinSystem
{
public:
	virtual void Tick(FFixedPoint DeltaTime, USeinWorldSubsystem& World) override
	{
		World.GetEntityPool().ForEachEntity([&](FSeinEntityHandle Handle, FSeinEntity& /*Entity*/)
		{
			const FSeinResourceIncomeData* IncomeComp = World.GetComponent<FSeinResourceIncomeData>(Handle);
			if (!IncomeComp || IncomeComp->IncomePerSecond.Num() == 0)
			{
				return;
			}

			FSeinPlayerID OwnerID = World.GetEntityOwner(Handle);
			if (!OwnerID.IsValid())
			{
				return;
			}

			FSeinPlayerState* PlayerState = World.GetPlayerState(OwnerID);
			if (!PlayerState)
			{
				return;
			}

			// Scale income by DeltaTime and add to the player's resources.
			FSeinResourceCost ScaledIncome;
			ScaledIncome.Amounts.Reserve(IncomeComp->IncomePerSecond.Num());

			for (const auto& Pair : IncomeComp->IncomePerSecond)
			{
				ScaledIncome.Amounts.Add(Pair.Key, Pair.Value * DeltaTime);
			}

			PlayerState->AddResources(ScaledIncome);
		});
	}

	virtual ESeinTickPhase GetPhase() const override { return ESeinTickPhase::PreTick; }
	virtual int32 GetPriority() const override { return 20; }
	virtual FName GetSystemName() const override { return TEXT("ResourceIncome"); }
};
