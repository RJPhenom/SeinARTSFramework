/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinDiplomacyBPFL.h
 * @brief   BP surface for diplomacy relations (DESIGN §18). Queries are
 *          direct subsystem reads; mutations route through the command
 *          buffer (+ are gated on `bAllowMidMatchDiplomacy` during Playing).
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Core/SeinPlayerID.h"
#include "Data/SeinDiplomacyTypes.h"
#include "SeinDiplomacyBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Diplomacy Library"))
class SEINARTSCOREENTITY_API USeinDiplomacyBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// Directional reads ---------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Diplomacy",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Diplomacy Tags"))
	static FGameplayTagContainer SeinGetDiplomacyTags(const UObject* WorldContextObject, FSeinPlayerID FromPlayer, FSeinPlayerID ToPlayer);

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Diplomacy",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Has Diplomacy Tag"))
	static bool SeinHasDiplomacyTag(const UObject* WorldContextObject, FSeinPlayerID FromPlayer, FSeinPlayerID ToPlayer, FGameplayTag Tag, bool bBidirectional = false);

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Diplomacy",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Pairs With Tag"))
	static TArray<FSeinDiplomacyKey> SeinGetPairsWithTag(const UObject* WorldContextObject, FGameplayTag Tag);

	// Bidirectional convenience checks (framework systems + UI) ----------

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Diplomacy",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Are Allied"))
	static bool SeinAreAllied(const UObject* WorldContextObject, FSeinPlayerID A, FSeinPlayerID B);

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Diplomacy",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Are At War"))
	static bool SeinAreAtWar(const UObject* WorldContextObject, FSeinPlayerID A, FSeinPlayerID B);

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Diplomacy",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Has Shared Vision"))
	static bool SeinHasSharedVision(const UObject* WorldContextObject, FSeinPlayerID Viewer, FSeinPlayerID Target);

	// Mutations (route through command buffer) ---------------------------

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Diplomacy",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Modify Diplomacy"))
	static void SeinModifyDiplomacy(const UObject* WorldContextObject, FSeinPlayerID FromPlayer, FSeinPlayerID ToPlayer, const FGameplayTagContainer& TagsToAdd, const FGameplayTagContainer& TagsToRemove);

	/** Convenience wrapper — adds `State.AtWar`, removes `State.Peace` + `.Truce`. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Diplomacy",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Declare War"))
	static void SeinDeclareWar(const UObject* WorldContextObject, FSeinPlayerID FromPlayer, FSeinPlayerID ToPlayer);

	/** Convenience wrapper — adds `State.Peace`, removes `State.AtWar`. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Diplomacy",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Declare Peace"))
	static void SeinDeclarePeace(const UObject* WorldContextObject, FSeinPlayerID FromPlayer, FSeinPlayerID ToPlayer);

	/** Convenience wrapper — adds `State.Truce`. Designer manages the expiry
	 *  (V1 framework doesn't auto-remove; pair a scheduled scenario ability
	 *  or effect if timed truces are needed). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Diplomacy",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Propose Truce"))
	static void SeinProposeTruce(const UObject* WorldContextObject, FSeinPlayerID FromPlayer, FSeinPlayerID ToPlayer);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
