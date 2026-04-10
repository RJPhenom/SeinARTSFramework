/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		Entity.h
 * @date:		2/28/2026
 * @author:		RJ Macklem
 * @brief:		Core entity data structure for simulation.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/EntityID.h"
#include "Types/Transform.h"
#include "Types/FixedPoint.h"
#include "Entity.generated.h"

/**
 * Core entity data that all simulation entities have.
 * Kept minimal to ensure cache-friendly iteration.
 * All gameplay data (health, damage, etc.) lives in modular components.
 */
USTRUCT(BlueprintType)
struct SEINARTSCORE_API FSeinEntity
{
	GENERATED_BODY()

	/** Unique identifier for this entity */
	UPROPERTY(BlueprintReadOnly, Category = "Entity")
	FSeinID ID;

	/** Current transform in simulation space (fixed-point) */
	UPROPERTY(BlueprintReadOnly, Category = "Entity")
	FFixedTransform Transform;

	/** Entity state flags (sim-side only — selection is render-side) */
	UPROPERTY(BlueprintReadOnly, Category = "Entity")
	int32 Flags;

	// State flag bits
	static constexpr int32 FLAG_ALIVE = 1 << 0;
	static constexpr int32 FLAG_INVULNERABLE = 1 << 1;

	FSeinEntity()
		: ID(FSeinID::Invalid())
		, Transform(FFixedTransform::Identity())
		, Flags(FLAG_ALIVE)
	{}

	explicit FSeinEntity(FSeinID InID)
		: ID(InID)
		, Transform(FFixedTransform::Identity())
		, Flags(FLAG_ALIVE)
	{}

	/** Check if entity is alive */
	FORCEINLINE bool IsAlive() const
	{
		return (Flags & FLAG_ALIVE) != 0;
	}

	/** Set alive flag */
	FORCEINLINE void SetAlive(bool bAlive)
	{
		if (bAlive)
		{
			Flags |= FLAG_ALIVE;
		}
		else
		{
			Flags &= ~FLAG_ALIVE;
		}
	}
};

/** Hash function for FSeinEntity for state hashing */
FORCEINLINE uint32 GetTypeHash(const FSeinEntity& Entity)
{
	uint32 Hash = GetTypeHash(Entity.ID);
	Hash = HashCombine(Hash, GetTypeHash(Entity.Transform));
	Hash = HashCombine(Hash, GetTypeHash(Entity.Flags));
	return Hash;
}
