/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLocomotion.cpp
 */

#include "Movement/SeinLocomotion.h"
#include "Math/MathLib.h"
#include "Types/Quat.h"
#include "Types/Vector.h"

FFixedPoint USeinLocomotion::ShortestAngleDelta(FFixedPoint From, FFixedPoint To)
{
	FFixedPoint Delta = To - From;
	// Wrap to [-π, π]. At most two iterations needed if inputs are already in
	// [-π, π] (typical case); guard with a loop anyway for safety.
	while (Delta > FFixedPoint::Pi)    { Delta = Delta - FFixedPoint::TwoPi; }
	while (Delta < -FFixedPoint::Pi)   { Delta = Delta + FFixedPoint::TwoPi; }
	return Delta;
}

FFixedPoint USeinLocomotion::ClampFP(FFixedPoint Val, FFixedPoint Min, FFixedPoint Max)
{
	if (Val < Min) return Min;
	if (Val > Max) return Max;
	return Val;
}

FFixedQuaternion USeinLocomotion::YawOnly(FFixedPoint YawRadians)
{
	// Roll=0, Pitch=0, Yaw=input. Matches MakeFromEulers' (X=roll, Y=pitch,
	// Z=yaw) convention.
	return FFixedQuaternion::MakeFromEulers(FFixedVector(FFixedPoint::Zero, FFixedPoint::Zero, YawRadians));
}

FFixedPoint USeinLocomotion::YawFromRotation(const FFixedQuaternion& Rotation)
{
	// Extract via the forward vector rather than Eulers() — Eulers has branches
	// for gimbal singularities at ±90° pitch that add cost we don't need for
	// upright yaw-only rotations.
	const FFixedVector Forward = Rotation.RotateVector(FFixedVector::ForwardVector);
	return SeinMath::Atan2(Forward.Y, Forward.X);
}
