/**
 * ▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄
 * ██░▄▄▄░█░▄▄██▄██░▄▄▀█░▄▄▀██░▄▄▀█▄▄░▄▄██░▄▄▄░
 * ██▄▄▄▀▀█░▄▄██░▄█░██░█░▀▀░██░▀▀▄███░████▄▄▄▀▀
 * ██░▀▀▀░█▄▄▄█▄▄▄█▄██▄█░██░██░██░███░████░▀▀▀░
 * ▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀
 * Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinSteeringBPFL.h
 * @date:		3/27/2026
 * @author:		RJ Macklem
 * @brief:		Blueprint Function Library for designer-composable steering primitives.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/SeinEntityHandle.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinSteeringBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Steering Library"))
class SEINARTSNAVIGATION_API USeinSteeringBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// Movement Primitives
	// ====================================================================================================

	/** Move an entity one tick in a direction at a given speed */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Steering", meta = (WorldContext = "WorldContextObject", DisplayName = "Sein Move In Direction"))
	static void SeinMoveInDirection(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FFixedVector Direction, FFixedPoint Speed, FFixedPoint DeltaTime);

	/** Rotate an entity toward a target direction at a given turn rate. Returns true when facing target. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Steering", meta = (WorldContext = "WorldContextObject", DisplayName = "Sein Rotate Toward"))
	static bool SeinRotateToward(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FFixedVector TargetDirection, FFixedPoint TurnRate, FFixedPoint DeltaTime);

	/** Teleport an entity to a new location instantly */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Steering", meta = (WorldContext = "WorldContextObject", DisplayName = "Sein Teleport Entity"))
	static void SeinTeleportEntity(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FFixedVector NewLocation);

	/** Apply turn rate limiting to a direction change (for wheeled vehicles) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Steering", meta = (DisplayName = "Sein Apply Turn Rate Limit"))
	static FFixedVector SeinApplyTurnRateLimit(FFixedVector CurrentDirection, FFixedVector DesiredDirection, FFixedPoint TurnRate, FFixedPoint DeltaTime);

	// Flocking Forces
	// ====================================================================================================

	/** Calculate separation force to avoid overlapping nearby entities */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Steering", meta = (DisplayName = "Sein Get Separation Force"))
	static FFixedVector SeinGetSeparationForce(FFixedVector MyPosition, const TArray<FFixedVector>& NearbyPositions, FFixedPoint Radius);

	/** Calculate cohesion force to stay near a group center */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Steering", meta = (DisplayName = "Sein Get Cohesion Force"))
	static FFixedVector SeinGetCohesionForce(FFixedVector MyPosition, const TArray<FFixedVector>& GroupPositions);

	/** Calculate alignment force to match average heading of nearby entities */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Steering", meta = (DisplayName = "Sein Get Alignment Force"))
	static FFixedVector SeinGetAlignmentForce(FFixedVector MyHeading, const TArray<FFixedVector>& NearbyHeadings);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
