#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Core/SeinEntityHandle.h"
#include "GameplayTagContainer.h"
#include "SeinPathTypes.generated.h"

/**
 * Describes a pathfinding request: start/end positions, requester, and movement constraints.
 */
USTRUCT(BlueprintType)
struct SEINARTSNAVIGATION_API FSeinPathRequest
{
	GENERATED_BODY()

	/** World-space start position. */
	UPROPERTY(BlueprintReadWrite, Category = "Path")
	FFixedVector Start;

	/** World-space destination position. */
	UPROPERTY(BlueprintReadWrite, Category = "Path")
	FFixedVector End;

	/** Entity that requested this path. */
	UPROPERTY(BlueprintReadWrite, Category = "Path")
	FSeinEntityHandle Requester;

	/** Maximum walkability cost this unit can traverse (1-255). */
	UPROPERTY(BlueprintReadWrite, Category = "Path")
	uint8 MaxMovementCost = 255;

	/** Terrain tags that this unit treats as impassable. */
	UPROPERTY(BlueprintReadWrite, Category = "Path")
	FGameplayTagContainer BlockedTerrainTags;
};

/**
 * Result of a pathfinding query: a sequence of world-space waypoints.
 */
USTRUCT(BlueprintType)
struct SEINARTSNAVIGATION_API FSeinPath
{
	GENERATED_BODY()

	/** Ordered list of world-space waypoints from start to end. */
	UPROPERTY(BlueprintReadOnly, Category = "Path")
	TArray<FFixedVector> Waypoints;

	/** Accumulated movement cost along the path (integer-scaled). */
	UPROPERTY(BlueprintReadOnly, Category = "Path")
	FFixedPoint TotalCost;

	/** True if a valid path was found. */
	UPROPERTY(BlueprintReadOnly, Category = "Path")
	bool bIsValid = false;

	/** True if the path does not reach the exact destination (partial path). */
	UPROPERTY(BlueprintReadOnly, Category = "Path")
	bool bIsPartial = false;

	/** Remove all waypoints and reset state. */
	void Clear()
	{
		Waypoints.Empty();
		TotalCost = FFixedPoint::Zero;
		bIsValid = false;
		bIsPartial = false;
	}

	/** Number of waypoints in the path. */
	int32 GetWaypointCount() const
	{
		return Waypoints.Num();
	}

	/** Return waypoint at given index, or zero vector if out of range. */
	FFixedVector GetWaypoint(int32 Index) const
	{
		if (Waypoints.IsValidIndex(Index))
		{
			return Waypoints[Index];
		}
		return FFixedVector();
	}
};
