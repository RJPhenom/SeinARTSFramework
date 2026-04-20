/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinProductionBPFL.h
 * @brief   Blueprint Function Library for production availability queries,
 *          tech tag checks, and production command helpers.
 *          UI widgets bind to these to show greyed-out / enabled buttons.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinPlayerID.h"
#include "Types/FixedPoint.h"
#include "GameplayTagContainer.h"
#include "Components/SeinProductionData.h"
#include "SeinProductionBPFL.generated.h"

class ASeinActor;
class USeinWorldSubsystem;

/**
 * Availability info for a single producible item in a building's queue.
 * Returned by SeinGetProductionAvailability — UI binds to this each frame
 * or on tech-change events to update button states (greyed-out, enabled, etc.).
 */
USTRUCT(BlueprintType)
struct SEINARTSCOREENTITY_API FSeinProductionAvailability
{
	GENERATED_BODY()

	/** The Blueprint class of the producible entity or research */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Production")
	TSubclassOf<ASeinActor> ActorClass;

	/** Display name from archetype definition */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Production")
	FText DisplayName;

	/** Icon from archetype definition */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Production")
	TObjectPtr<UTexture2D> Icon;

	/** Resource cost to produce */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Production")
	TMap<FName, FFixedPoint> Cost;

	/** Build time in sim-seconds */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Production")
	FFixedPoint BuildTime;

	/** Archetype gameplay tag (used to issue QueueProduction commands) */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Production")
	FGameplayTag ArchetypeTag;

	/** True if the player has all prerequisite tech tags */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Production")
	bool bPrerequisitesMet = false;

	/** True if the player can currently afford the cost */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Production")
	bool bCanAfford = false;

	/** True if the production queue is full */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Production")
	bool bQueueFull = false;

	/** True if this is a research item that has already been completed */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Production")
	bool bAlreadyResearched = false;

	/** True if this item is a research entry (not a unit) */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Production")
	bool bIsResearch = false;

	/** Returns true if this item can be queued right now. */
	bool CanQueue() const
	{
		return bPrerequisitesMet && bCanAfford && !bQueueFull && !bAlreadyResearched;
	}
};

UCLASS(meta = (DisplayName = "SeinARTS Production Library"))
class SEINARTSCOREENTITY_API USeinProductionBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// Read Component Data
	// ====================================================================================================

	/** Read FSeinProductionData for an entity. Returns false and logs a warning on invalid
	 *  handle or missing component; OutData is untouched on failure. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Production",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Production Data"))
	static bool SeinGetProductionData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FSeinProductionData& OutData);

	/** Batch read FSeinProductionData. Invalid/missing entities are skipped (warning logged). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Production",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Production Data"))
	static TArray<FSeinProductionData> SeinGetProductionDataMany(const UObject* WorldContextObject, const TArray<FSeinEntityHandle>& EntityHandles);

	// Availability / Tech
	// ====================================================================================================

	/**
	 * Get production availability for all items a building can produce.
	 * Call each frame (or on tech change) to update UI button states.
	 * @param WorldContextObject - World context
	 * @param PlayerID - Player checking availability
	 * @param BuildingEntity - Entity handle of the production building
	 * @return Array of availability info, one per producible class
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Production",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Production Availability"))
	static TArray<FSeinProductionAvailability> SeinGetProductionAvailability(
		const UObject* WorldContextObject,
		FSeinPlayerID PlayerID,
		FSeinEntityHandle BuildingEntity);

	/** Quick check: can a player produce a specific actor class right now? */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Production",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Can Player Produce"))
	static bool SeinCanPlayerProduce(
		const UObject* WorldContextObject,
		FSeinPlayerID PlayerID,
		TSubclassOf<ASeinActor> ActorClass);

	/** Check if a player has a specific tech tag. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Production",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Player Has Tech Tag"))
	static bool SeinPlayerHasTechTag(
		const UObject* WorldContextObject,
		FSeinPlayerID PlayerID,
		FGameplayTag TechTag);

	/** Get all unlocked tech tags for a player. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Production",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Player Tech Tags"))
	static FGameplayTagContainer SeinGetPlayerTechTags(
		const UObject* WorldContextObject,
		FSeinPlayerID PlayerID);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
