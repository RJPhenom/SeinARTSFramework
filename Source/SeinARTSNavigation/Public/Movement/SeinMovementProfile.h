/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementProfile.h
 * @brief   Pluggable per-unit kinematic profile for path advancement.
 *
 * Profiles are stateless, CDO-shared config objects. Per-instance transient state
 * (current speed, etc.) lives in FSeinMovementKinematicState owned by the action.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Movement/SeinMovementKinematicState.h"
#include "SeinMovementProfile.generated.h"

class USeinPathfinder;
struct FSeinEntity;
struct FSeinMovementComponent;
struct FSeinPath;
struct FSeinPathRequest;

UCLASS(Abstract, Blueprintable, EditInlineNew)
class SEINARTSNAVIGATION_API USeinMovementProfile : public UObject
{
	GENERATED_BODY()

public:
	/** Build a path for this unit. Default impl calls the pathfinder directly. */
	virtual void BuildPath(USeinPathfinder* Pathfinder, const FSeinPathRequest& Request, FSeinPath& OutPath) const;

	/**
	 * Advance the entity along the path by DeltaTime. Updates Entity transform in place.
	 * @param Entity              Sim entity being moved.
	 * @param MoveComp            The entity's movement component (speed/accel/turn).
	 * @param Path                The path to follow.
	 * @param WaypointIndex       (in-out) current waypoint being approached.
	 * @param FinalDestination    Absolute final destination.
	 * @param AcceptanceRadiusSq  Squared arrival tolerance.
	 * @param DeltaTime           Sim tick delta.
	 * @param KinState            (in-out) transient kinematic state (current speed).
	 * @param OutWaypointReached  Set to the waypoint index consumed this tick, or -1.
	 * @return true when the entity has reached the final destination.
	 */
	virtual bool AdvanceAlongPath(
		FSeinEntity& Entity,
		const FSeinMovementComponent& MoveComp,
		const FSeinPath& Path,
		int32& WaypointIndex,
		const FFixedVector& FinalDestination,
		FFixedPoint AcceptanceRadiusSq,
		FFixedPoint DeltaTime,
		FSeinMovementKinematicState& KinState,
		int32& OutWaypointReached) const PURE_VIRTUAL(USeinMovementProfile::AdvanceAlongPath, return true;);
};
