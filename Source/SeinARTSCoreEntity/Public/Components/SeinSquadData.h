/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSquadData.h
 * @brief   Squad component for the abstract squad entity that manages members.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Types/FixedPoint.h"
#include "Core/SeinEntityHandle.h"
#include "Actor/SeinActor.h"
#include "Components/SeinComponent.h"
#include "SeinSquadData.generated.h"

/**
 * Squad component placed on the abstract squad entity.
 * Tracks squad members, leadership, and reinforcement parameters.
 */
USTRUCT(BlueprintType)
struct SEINARTSCOREENTITY_API FSeinSquadData : public FSeinComponent
{
	GENERATED_BODY()

	/** Handles to all living squad member entities */
	UPROPERTY(BlueprintReadOnly, Category = "Squad")
	TArray<FSeinEntityHandle> Members;

	/** Handle to the current squad leader */
	UPROPERTY(BlueprintReadOnly, Category = "Squad")
	FSeinEntityHandle Leader;

	/** Maximum number of members allowed in this squad */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Squad")
	int32 MaxSquadSize = 6;

	/** Blueprint class used when reinforcing this squad */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Squad")
	TSubclassOf<ASeinActor> MemberActorClass;

	/** Resource cost to reinforce one member */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Squad")
	TMap<FName, FFixedPoint> ReinforceCost;

	/** Max distance members can be from the squad center before losing coherency */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Squad")
	FFixedPoint CoherencyRadius;

	/** Returns current member count. External systems must validate entity liveness. */
	int32 GetAliveCount() const;

	/** Returns true if the leader handle is valid */
	bool HasLeader() const;

	/** Remove a member from the squad by handle */
	void RemoveMember(FSeinEntityHandle Member);

	/** Add a new member to the squad */
	void AddMember(FSeinEntityHandle Member);

	/** Promote the first member to leader. Clears leader if no members remain. */
	void PromoteNewLeader();
};

FORCEINLINE uint32 GetTypeHash(const FSeinSquadData& Component)
{
	uint32 Hash = GetTypeHash(Component.Leader);
	Hash = HashCombine(Hash, GetTypeHash(Component.MaxSquadSize));
	Hash = HashCombine(Hash, GetTypeHash(Component.CoherencyRadius));
	for (const auto& Member : Component.Members)
	{
		Hash = HashCombine(Hash, GetTypeHash(Member));
	}
	for (const auto& Pair : Component.ReinforceCost)
	{
		Hash = HashCombine(Hash, GetTypeHash(Pair.Key));
		Hash = HashCombine(Hash, GetTypeHash(Pair.Value));
	}
	return Hash;
}
