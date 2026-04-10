/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSquadMemberComponent.h
 * @brief   Component on individual squad member entities linking back to their squad.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Types/Vector.h"
#include "Core/SeinEntityHandle.h"
#include "SeinSquadMemberComponent.generated.h"

/**
 * Placed on each individual entity that belongs to a squad.
 * Links back to the owning squad entity and stores formation data.
 */
USTRUCT(BlueprintType)
struct SEINARTSCOREENTITY_API FSeinSquadMemberComponent : public FTableRowBase
{
	GENERATED_BODY()

	/** Handle to the squad entity this member belongs to */
	UPROPERTY(BlueprintReadOnly, Category = "Squad")
	FSeinEntityHandle SquadEntity;

	/** Whether this member is the current leader of the squad */
	UPROPERTY(BlueprintReadOnly, Category = "Squad")
	bool bIsLeader = false;

	/** Offset from the squad center for formation positioning */
	UPROPERTY(BlueprintReadOnly, Category = "Squad")
	FFixedVector FormationOffset;
};

FORCEINLINE uint32 GetTypeHash(const FSeinSquadMemberComponent& Component)
{
	uint32 Hash = GetTypeHash(Component.SquadEntity);
	Hash = HashCombine(Hash, GetTypeHash(Component.bIsLeader));
	Hash = HashCombine(Hash, GetTypeHash(Component.FormationOffset));
	return Hash;
}
