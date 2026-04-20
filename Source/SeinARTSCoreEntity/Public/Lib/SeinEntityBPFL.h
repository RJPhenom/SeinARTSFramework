/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinEntityBPFL.h
 * @date:		3/27/2026
 * @author:		RJ Macklem
 * @brief:		Blueprint Function Library for entity lifecycle and queries.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinPlayerID.h"
#include "Types/Transform.h"
#include "SeinEntityBPFL.generated.h"

class USeinWorldSubsystem;
class USeinArchetypeDefinition;
class ASeinActor;

UCLASS(meta = (DisplayName = "SeinARTS Entity Library"))
class SEINARTSCOREENTITY_API USeinEntityBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// Entity Lifecycle
	// ====================================================================================================

	/** Spawn an entity from a Blueprint class at the given transform, owned by the specified player */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Entity", meta = (WorldContext = "WorldContextObject", DisplayName = "Spawn Entity"))
	static FSeinEntityHandle SeinSpawnEntity(const UObject* WorldContextObject, TSubclassOf<ASeinActor> ActorClass, const FFixedTransform& SpawnTransform, FSeinPlayerID OwnerPlayerID);

	/** Destroy an entity (deferred to post-tick cleanup) */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Entity", meta = (WorldContext = "WorldContextObject", DisplayName = "Destroy Entity"))
	static void SeinDestroyEntity(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle);

	// Entity Queries
	// ====================================================================================================

	/** Get the simulation transform of an entity */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Entity Transform"))
	static FFixedTransform SeinGetEntityTransform(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle);

	/** Set the simulation transform of an entity */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Entity", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Entity Transform"))
	static void SeinSetEntityTransform(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, const FFixedTransform& Transform);

	/** Get the owner player ID of an entity */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Entity Owner"))
	static FSeinPlayerID SeinGetEntityOwner(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle);

	/** Check whether an entity handle refers to a living entity */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity", meta = (WorldContext = "WorldContextObject", DisplayName = "Is Entity Alive"))
	static bool SeinIsEntityAlive(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle);

	/** Check whether an entity handle is valid (non-zero generation) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity", meta = (DisplayName = "Is Handle Valid"))
	static bool SeinIsHandleValid(FSeinEntityHandle EntityHandle);

	/** Returns an invalid entity handle */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity", meta = (DisplayName = "Invalid Handle"))
	static FSeinEntityHandle SeinInvalidHandle();

	// Archetype Access
	// ====================================================================================================

	/** Read the entity's archetype definition (identity/cost/tech metadata from the authoring CDO).
	 *  Returns nullptr if the handle is invalid or the spawning BP has no ArchetypeDefinition set. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Archetype Definition"))
	static USeinArchetypeDefinition* SeinGetArchetypeDefinition(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle);

	/** Batch read archetype definitions. Null entries are returned for invalid handles (preserves input order). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Archetype Definition"))
	static TArray<USeinArchetypeDefinition*> SeinGetArchetypeDefinitionMany(const UObject* WorldContextObject, const TArray<FSeinEntityHandle>& EntityHandles);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
