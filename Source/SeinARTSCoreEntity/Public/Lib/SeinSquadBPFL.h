/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinSquadBPFL.h
 * @date:		3/27/2026
 * @author:		RJ Macklem
 * @brief:		Blueprint Function Library for squad queries.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/SeinEntityHandle.h"
#include "SeinSquadBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Squad Library"))
class SEINARTSCOREENTITY_API USeinSquadBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** Get all members of a squad entity */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Squad", meta = (WorldContext = "WorldContextObject", DisplayName = "Sein Get Squad Members"))
	static TArray<FSeinEntityHandle> SeinGetSquadMembers(const UObject* WorldContextObject, FSeinEntityHandle SquadHandle);

	/** Get the current leader of a squad */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Squad", meta = (WorldContext = "WorldContextObject", DisplayName = "Sein Get Squad Leader"))
	static FSeinEntityHandle SeinGetSquadLeader(const UObject* WorldContextObject, FSeinEntityHandle SquadHandle);

	/** Get the squad entity that a member belongs to */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Squad", meta = (WorldContext = "WorldContextObject", DisplayName = "Sein Get Entity Squad"))
	static FSeinEntityHandle SeinGetEntitySquad(const UObject* WorldContextObject, FSeinEntityHandle MemberHandle);

	/** Check whether an entity is a member of any squad */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Squad", meta = (WorldContext = "WorldContextObject", DisplayName = "Sein Is Squad Member"))
	static bool SeinIsSquadMember(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle);

	/** Get the number of alive members in a squad */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Squad", meta = (WorldContext = "WorldContextObject", DisplayName = "Sein Get Squad Size"))
	static int32 SeinGetSquadSize(const UObject* WorldContextObject, FSeinEntityHandle SquadHandle);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
