#pragma once

#include "CoreMinimal.h"
#include "Movement/SeinMovementProfile.h"
#include "SeinTrackedMovementProfile.generated.h"

/**
 * Tracked vehicle (tanks, APCs). Can rotate in place at zero velocity.
 * Full turn rate regardless of current speed. Brakes, pivots, then accelerates.
 * Any grid corner navigable.
 */
UCLASS(DisplayName = "Tracked Movement Profile")
class SEINARTSNAVIGATION_API USeinTrackedMovementProfile : public USeinMovementProfile
{
	GENERATED_BODY()

public:
	/** Dot product threshold above which the vehicle accelerates forward instead of braking to pivot. */
	UPROPERTY(EditAnywhere, Category = "Tracked")
	FFixedPoint AlignmentForwardThreshold = FFixedPoint::FromFloat(0.95f);

	virtual bool AdvanceAlongPath(
		FSeinEntity& Entity,
		const FSeinMovementComponent& MoveComp,
		const FSeinPath& Path,
		int32& WaypointIndex,
		const FFixedVector& FinalDestination,
		FFixedPoint AcceptanceRadiusSq,
		FFixedPoint DeltaTime,
		FSeinMovementKinematicState& KinState,
		int32& OutWaypointReached) const override;
};
