/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTrackedVehicleMovement.h
 * @brief   Tracked-vehicle movement — can rotate in place, moves forward-only.
 *
 *          Tracked vehicles (tanks, bulldozers) steer by differential track
 *          rotation — they CAN pivot on the spot, unlike wheeled vehicles.
 *          Behavior:
 *          - If the aim-angle to the next waypoint exceeds `PivotThreshold`
 *            (default 45°), the vehicle ROTATES IN PLACE at TurnRate until
 *            aligned enough to move.
 *          - Otherwise it rotates-while-moving like a wheeled vehicle, but
 *            typically with a slower TurnRate (set per-unit on
 *            FSeinMovementData).
 *
 *          Motion is always along the current forward vector (forward-only);
 *          no strafing.
 */

#pragma once

#include "CoreMinimal.h"
#include "Movement/SeinLocomotion.h"
#include "SeinTrackedVehicleMovement.generated.h"

UCLASS(meta = (DisplayName = "Tracked Vehicle"))
class SEINARTSNAVIGATION_API USeinTrackedVehicleMovement : public USeinLocomotion
{
	GENERATED_BODY()

public:

	/** Above this aim-angle (radians) the vehicle stops and rotates in place
	 *  until back under the threshold. Default: π/4 (45°). Lower values keep
	 *  the tank moving through sharper turns; higher values force more
	 *  "stop-and-turn" beats. */
	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "0.0"))
	float PivotThresholdRadians = 0.785398f; // π/4

	virtual bool Tick(
		FSeinEntity& Entity,
		const FSeinMovementData& MoveData,
		const FSeinPath& Path,
		int32& CurrentWaypointIndex,
		FFixedPoint AcceptanceRadiusSq,
		FFixedPoint DeltaTime,
		USeinNavigation* Nav) override;
};
