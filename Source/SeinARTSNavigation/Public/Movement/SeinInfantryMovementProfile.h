#pragma once

#include "CoreMinimal.h"
#include "Movement/SeinMovementProfile.h"
#include "SeinInfantryMovementProfile.generated.h"

/**
 * Hyper-responsive movement. Instant rotation, no acceleration ramp.
 * Current baseline behavior — byte-for-byte parity with the original
 * USeinMoveToAction::TickAction linear waypoint budget implementation.
 */
UCLASS(DisplayName = "Infantry Movement Profile")
class SEINARTSNAVIGATION_API USeinInfantryMovementProfile : public USeinMovementProfile
{
	GENERATED_BODY()

public:
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
