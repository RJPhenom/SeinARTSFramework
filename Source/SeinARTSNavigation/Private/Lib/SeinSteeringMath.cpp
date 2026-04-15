#include "Lib/SeinSteeringMath.h"
#include "Math/MathLib.h"

FFixedVector SeinSteeringMath::ApplyTurnRateLimit(FFixedVector CurrentDirection, FFixedVector DesiredDirection, FFixedPoint TurnRate, FFixedPoint DeltaTime)
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

	FFixedPoint Alpha = MaxTurn / AngleBetween;
	FFixedVector Result = NormCurrent * (FFixedPoint::One - Alpha) + NormDesired * Alpha;
	FFixedPoint ResultLen = Result.Size();
	if (ResultLen > FFixedPoint::Zero)
	{
		return Result / ResultLen;
	}
	return NormCurrent;
}
