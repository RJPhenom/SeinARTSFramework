/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBasicMovement.h
 * @brief   Baseline movement algo — seek current waypoint at MoveSpeed,
 *          arrive within a one-step radius, increment to next waypoint.
 *
 *          No rotation, no avoidance, no inertia. Matches the hardcoded
 *          behavior that lived in MoveToAction prior to the algo refactor.
 *          Shipped as the framework default so "works out of the box"
 *          stays preserved for any unit that doesn't pick a specific algo.
 */

#pragma once

#include "CoreMinimal.h"
#include "Movement/SeinLocomotion.h"
#include "SeinBasicMovement.generated.h"

UCLASS(meta = (DisplayName = "Basic (Seek + Arrive)"))
class SEINARTSNAVIGATION_API USeinBasicMovement : public USeinLocomotion
{
	GENERATED_BODY()

public:
	virtual bool Tick(
		FSeinEntity& Entity,
		const FSeinMovementData& MoveData,
		const FSeinPath& Path,
		int32& CurrentWaypointIndex,
		FFixedPoint AcceptanceRadiusSq,
		FFixedPoint DeltaTime,
		USeinNavigation* Nav) override;
};
