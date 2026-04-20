#pragma once

#include "CoreMinimal.h"
#include "Movement/SeinMovementProfile.h"
#include "SeinWheeledMovementProfile.generated.h"

/**
 * Wheeled vehicle (cars, trucks, halftracks). Cannot rotate at zero velocity.
 * Effective turn rate scales with current forward speed, producing a real
 * turn radius of approximately MoveSpeed / TurnRate. On tight corners the
 * vehicle will reverse briefly as a crude 3-point turn fallback.
 *
 * Note: proper Dubins/Reeds-Shepp path smoothing is deferred. The reverse
 * fallback may oscillate on pathological corners (S-curves in narrow streets).
 */
UCLASS(DisplayName = "Wheeled Movement Profile")
class SEINARTSNAVIGATION_API USeinWheeledMovementProfile : public USeinMovementProfile
{
	GENERATED_BODY()

public:
	/** Dot above which vehicle applies full throttle. */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Movement|Wheeled")
	FFixedPoint AlignmentFullThrottle = FFixedPoint::FromFloat(0.9f);

	/** Dot above which vehicle applies partial throttle (arcing turn). */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Movement|Wheeled")
	FFixedPoint AlignmentPartialThrottle = FFixedPoint::FromFloat(0.3f);

	/** Target-speed fraction during partial throttle turn. */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Movement|Wheeled")
	FFixedPoint PartialThrottleSpeedFraction = FFixedPoint::FromFloat(0.5f);

	/** Target-speed fraction (negative) during reverse-maneuver. */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Movement|Wheeled")
	FFixedPoint ReverseSpeedFraction = FFixedPoint::FromFloat(0.3f);

	virtual bool AdvanceAlongPath(
		FSeinEntity& Entity,
		const FSeinMovementData& MoveComp,
		const FSeinPath& Path,
		int32& WaypointIndex,
		const FFixedVector& FinalDestination,
		FFixedPoint AcceptanceRadiusSq,
		FFixedPoint DeltaTime,
		FSeinMovementKinematicState& KinState,
		int32& OutWaypointReached) const override;
};
