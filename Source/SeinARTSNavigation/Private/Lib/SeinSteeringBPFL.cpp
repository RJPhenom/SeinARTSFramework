/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSteeringBPFL.cpp
 * @brief   Implementation of steering primitive Blueprint nodes.
 */

#include "Lib/SeinSteeringBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Math/MathLib.h"
#include "Types/Rotator.h"
#include "Types/Quat.h"

USeinWorldSubsystem* USeinSteeringBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

void USeinSteeringBPFL::SeinMoveInDirection(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FFixedVector Direction, FFixedPoint Speed, FFixedPoint DeltaTime)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return;

	FSeinEntity* Entity = Subsystem->GetEntity(EntityHandle);
	if (!Entity || !Entity->IsAlive()) return;

	FFixedVector Velocity = Direction * Speed * DeltaTime;
	FFixedVector NewLocation = Entity->Transform.GetLocation() + Velocity;
	Entity->Transform.SetLocation(NewLocation);
}

bool USeinSteeringBPFL::SeinRotateToward(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FFixedVector TargetDirection, FFixedPoint TurnRate, FFixedPoint DeltaTime)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	FSeinEntity* Entity = Subsystem->GetEntity(EntityHandle);
	if (!Entity || !Entity->IsAlive()) return false;

	FFixedVector CurrentForward = Entity->Transform.GetQuaternionRotation().GetForwardVector();
	FFixedVector DesiredDir = SeinApplyTurnRateLimit(CurrentForward, TargetDirection, TurnRate, DeltaTime);

	// Build a yaw-only rotation from the desired direction
	FFixedPoint Yaw = SeinMath::Atan2(DesiredDir.Y, DesiredDir.X);
	FFixedRotator NewRotation(FFixedPoint::Zero, Yaw, FFixedPoint::Zero);
	Entity->Transform.SetRotation(NewRotation.Quaternion());

	// Check if we're facing the target (dot product close to 1)
	FFixedPoint Dot = FFixedVector::DotProduct(DesiredDir, TargetDirection);
	return Dot >= FFixedPoint::FromFloat(0.999f);
}

void USeinSteeringBPFL::SeinTeleportEntity(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FFixedVector NewLocation)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return;

	FSeinEntity* Entity = Subsystem->GetEntity(EntityHandle);
	if (!Entity) return;

	Entity->Transform.SetLocation(NewLocation);
}

FFixedVector USeinSteeringBPFL::SeinApplyTurnRateLimit(FFixedVector CurrentDirection, FFixedVector DesiredDirection, FFixedPoint TurnRate, FFixedPoint DeltaTime)
{
	FFixedPoint CurrentLen = CurrentDirection.Size();
	FFixedPoint DesiredLen = DesiredDirection.Size();
	if (CurrentLen <= FFixedPoint::Zero || DesiredLen <= FFixedPoint::Zero)
	{
		return CurrentDirection;
	}

	FFixedVector NormCurrent = CurrentDirection / CurrentLen;
	FFixedVector NormDesired = DesiredDirection / DesiredLen;

	FFixedPoint Dot = FFixedVector::DotProduct(NormCurrent, NormDesired);
	Dot = SeinMath::Clamp(Dot, -FFixedPoint::One, FFixedPoint::One);

	FFixedPoint AngleBetween = SeinMath::Acos(Dot);
	FFixedPoint MaxTurn = TurnRate * DeltaTime;

	if (AngleBetween <= MaxTurn)
	{
		return NormDesired;
	}

	// Slerp-like interpolation: blend toward desired by MaxTurn/AngleBetween ratio
	FFixedPoint Alpha = MaxTurn / AngleBetween;
	FFixedVector Result = NormCurrent * (FFixedPoint::One - Alpha) + NormDesired * Alpha;
	FFixedPoint ResultLen = Result.Size();
	if (ResultLen > FFixedPoint::Zero)
	{
		return Result / ResultLen;
	}
	return NormCurrent;
}

FFixedVector USeinSteeringBPFL::SeinGetSeparationForce(FFixedVector MyPosition, const TArray<FFixedVector>& NearbyPositions, FFixedPoint Radius)
{
	FFixedVector Force = FFixedVector::ZeroVector;
	if (NearbyPositions.Num() == 0 || Radius <= FFixedPoint::Zero) return Force;

	FFixedPoint RadiusSq = Radius * Radius;

	for (const FFixedVector& Other : NearbyPositions)
	{
		FFixedVector Delta = MyPosition - Other;
		FFixedPoint DistSq = FFixedVector::DotProduct(Delta, Delta);
		if (DistSq > FFixedPoint::Zero && DistSq < RadiusSq)
		{
			FFixedPoint Dist = Delta.Size();
			if (Dist > FFixedPoint::Zero)
			{
				// Stronger repulsion at closer distances
				Force = Force + (Delta / Dist) * (FFixedPoint::One - Dist / Radius);
			}
		}
	}

	return Force;
}

FFixedVector USeinSteeringBPFL::SeinGetCohesionForce(FFixedVector MyPosition, const TArray<FFixedVector>& GroupPositions)
{
	if (GroupPositions.Num() == 0) return FFixedVector::ZeroVector;

	FFixedVector Center = FFixedVector::ZeroVector;
	for (const FFixedVector& Pos : GroupPositions)
	{
		Center = Center + Pos;
	}
	Center = Center / FFixedPoint::FromInt(GroupPositions.Num());

	FFixedVector ToCenter = Center - MyPosition;
	FFixedPoint Len = ToCenter.Size();
	if (Len > FFixedPoint::Zero)
	{
		return ToCenter / Len;
	}
	return FFixedVector::ZeroVector;
}

FFixedVector USeinSteeringBPFL::SeinGetAlignmentForce(FFixedVector MyHeading, const TArray<FFixedVector>& NearbyHeadings)
{
	if (NearbyHeadings.Num() == 0) return FFixedVector::ZeroVector;

	FFixedVector AvgHeading = FFixedVector::ZeroVector;
	for (const FFixedVector& Heading : NearbyHeadings)
	{
		AvgHeading = AvgHeading + Heading;
	}
	AvgHeading = AvgHeading / FFixedPoint::FromInt(NearbyHeadings.Num());

	FFixedPoint Len = AvgHeading.Size();
	if (Len > FFixedPoint::Zero)
	{
		return AvgHeading / Len;
	}
	return FFixedVector::ZeroVector;
}
