/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinPathTypes.h
 * @brief   Shared path query / result structs. Kept impl-agnostic — any
 *          USeinNavigation subclass consumes FSeinPathRequest and produces
 *          FSeinPath.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Core/SeinEntityHandle.h"
#include "GameplayTagContainer.h"
#include "SeinPathTypes.generated.h"

/** Pathfinding query. Impl-specific fields (cost caps, agent class) live on
 *  the nav subclass; keep this struct small + engine-agnostic. */
USTRUCT(BlueprintType)
struct SEINARTSNAVIGATION_API FSeinPathRequest
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Navigation|Path")
	FFixedVector Start;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Navigation|Path")
	FFixedVector End;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Navigation|Path")
	FSeinEntityHandle Requester;

	/** Terrain tags this agent treats as impassable. Empty = no filter. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Navigation|Path")
	FGameplayTagContainer BlockedTerrainTags;

	/** Agent layer mask — which layer bits identify the requesting agent
	 *  for nav-blocker intersection. The pathfinder skips dynamic blockers
	 *  whose `BlockedNavLayerMask & AgentNavLayerMask == 0`. Default 0xFF
	 *  (matches all blockers — preserves single-layer behavior when the
	 *  caller doesn't fill this in). MoveToAction populates from
	 *  FSeinMovementData::NavLayerMask at request time. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Navigation|Path")
	uint8 AgentNavLayerMask = 0xFF;
};

/** Path query result. Waypoints are world-space, ordered Start → End. */
USTRUCT(BlueprintType)
struct SEINARTSNAVIGATION_API FSeinPath
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Navigation|Path")
	TArray<FFixedVector> Waypoints;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Navigation|Path")
	FFixedPoint TotalCost;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Navigation|Path")
	bool bIsValid = false;

	/** True if the path does not reach the exact destination (partial / best-effort). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Navigation|Path")
	bool bIsPartial = false;

	void Clear()
	{
		Waypoints.Reset();
		TotalCost = FFixedPoint::Zero;
		bIsValid = false;
		bIsPartial = false;
	}

	int32 GetWaypointCount() const { return Waypoints.Num(); }

	FFixedVector GetWaypoint(int32 Index) const
	{
		return Waypoints.IsValidIndex(Index) ? Waypoints[Index] : FFixedVector();
	}
};
