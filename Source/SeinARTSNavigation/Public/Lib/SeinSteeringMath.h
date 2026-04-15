/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSteeringMath.h
 * @brief   Shared fixed-point steering primitives used by profiles + BPFL.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"

namespace SeinSteeringMath
{
	/**
	 * Slerp-like turn-rate-limited rotation from Current toward Desired.
	 * Returns a unit-length direction. If either input is degenerate, returns Current.
	 */
	SEINARTSNAVIGATION_API FFixedVector ApplyTurnRateLimit(
		FFixedVector CurrentDirection,
		FFixedVector DesiredDirection,
		FFixedPoint TurnRate,
		FFixedPoint DeltaTime);
}
