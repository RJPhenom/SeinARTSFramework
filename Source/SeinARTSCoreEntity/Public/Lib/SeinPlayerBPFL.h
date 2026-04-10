/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinPlayerBPFL.h
 * @date:		3/27/2026
 * @author:		RJ Macklem
 * @brief:		Blueprint Function Library for player resources and entity queries.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinPlayerID.h"
#include "Types/FixedPoint.h"
#include "SeinPlayerBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Player Library"))
class SEINARTSCOREENTITY_API USeinPlayerBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// Resources
	// ====================================================================================================

	/** Get a named resource amount for a player */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Player", meta = (WorldContext = "WorldContextObject", DisplayName = "Sein Get Player Resource"))
	static FFixedPoint SeinGetPlayerResource(const UObject* WorldContextObject, FSeinPlayerID PlayerID, FName ResourceName);

	/** Add a named resource to a player's stockpile */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Player", meta = (WorldContext = "WorldContextObject", DisplayName = "Sein Add Player Resource"))
	static void SeinAddPlayerResource(const UObject* WorldContextObject, FSeinPlayerID PlayerID, FName ResourceName, FFixedPoint Amount);

	/** Deduct a named resource from a player. Returns false if insufficient. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Player", meta = (WorldContext = "WorldContextObject", DisplayName = "Sein Deduct Player Resource"))
	static bool SeinDeductPlayerResource(const UObject* WorldContextObject, FSeinPlayerID PlayerID, FName ResourceName, FFixedPoint Amount);

	/** Check whether a player can afford a set of resource costs */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Player", meta = (WorldContext = "WorldContextObject", DisplayName = "Sein Can Player Afford"))
	static bool SeinCanPlayerAfford(const UObject* WorldContextObject, FSeinPlayerID PlayerID, const TMap<FName, FFixedPoint>& Cost);

	// Entity Queries
	// ====================================================================================================

	/** Get all entities owned by a specific player */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Player", meta = (WorldContext = "WorldContextObject", DisplayName = "Sein Get Player Entities"))
	static TArray<FSeinEntityHandle> SeinGetPlayerEntities(const UObject* WorldContextObject, FSeinPlayerID PlayerID);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
