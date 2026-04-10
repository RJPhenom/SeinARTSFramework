/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFaction.h
 * @brief   UDataAsset defining a playable faction: its identity, starting
 *          resources, available archetypes, and starting entities.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Core/SeinFactionID.h"
#include "Types/FixedPoint.h"
#include "Actor/SeinActor.h"
#include "SeinFaction.generated.h"

/**
 * Data asset defining a playable faction.
 * Designers create one per faction to configure identity, economy, and roster.
 */
UCLASS(BlueprintType)
class SEINARTSCOREENTITY_API USeinFaction : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Display name of this faction */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Faction")
	FText FactionName;

	/** Unique faction identifier used by the simulation */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Faction")
	FSeinFactionID FactionID;

	/** Blueprint classes this faction can build/produce */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Faction")
	TArray<TSubclassOf<ASeinActor>> AvailableUnitClasses;

	/** Starting resources granted to players of this faction */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy")
	TMap<FName, FFixedPoint> StartingResources;

	/** Entity classes to spawn on game start (e.g., HQ building) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Faction")
	TArray<TSubclassOf<ASeinActor>> StartingEntityClasses;
};
