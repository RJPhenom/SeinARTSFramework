#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "SeinMovementKinematicState.generated.h"

/**
 * Transient per-action kinematic state used by movement profiles.
 * Lives on USeinMoveToAction (not on the profile — profiles are shared CDO config).
 * Infantry profile ignores this; Tracked/Wheeled read/write CurrentSpeed each tick.
 */
USTRUCT()
struct SEINARTSNAVIGATION_API FSeinMovementKinematicState
{
	GENERATED_BODY()

	/** Signed current forward speed. Negative = reversing (wheeled 3-point-turn). */
	UPROPERTY()
	FFixedPoint CurrentSpeed = FFixedPoint::Zero;
};
