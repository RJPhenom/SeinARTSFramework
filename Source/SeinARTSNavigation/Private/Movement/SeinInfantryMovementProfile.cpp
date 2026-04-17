#include "Movement/SeinInfantryMovementProfile.h"
#include "SeinPathTypes.h"
#include "Types/Entity.h"
#include "Types/Vector.h"
#include "Components/SeinMovementData.h"

bool USeinInfantryMovementProfile::AdvanceAlongPath(
	FSeinEntity& Entity,
	const FSeinMovementData& MoveComp,
	const FSeinPath& Path,
	int32& WaypointIndex,
	const FFixedVector& FinalDestination,
	FFixedPoint AcceptanceRadiusSq,
	FFixedPoint DeltaTime,
	FSeinMovementKinematicState& /*KinState*/,
	int32& OutWaypointReached) const
{
	OutWaypointReached = -1;

	const FFixedPoint Speed = MoveComp.MoveSpeed;
	FFixedPoint RemainingBudget = Speed * DeltaTime;
	FFixedVector CurrentPos = Entity.Transform.GetLocation();
	const int32 WaypointCount = Path.GetWaypointCount();

	while (WaypointIndex < WaypointCount && RemainingBudget > FFixedPoint::Zero)
	{
		const FFixedVector& Waypoint = Path.Waypoints[WaypointIndex];
		const FFixedVector ToWaypoint = Waypoint - CurrentPos;
		const FFixedPoint DistSq = ToWaypoint.SizeSquared();

		const FFixedPoint BudgetSq = RemainingBudget * RemainingBudget;
		if (DistSq <= BudgetSq)
		{
			const FFixedPoint Dist = ToWaypoint.Size();
			RemainingBudget = RemainingBudget - Dist;
			CurrentPos = Waypoint;
			OutWaypointReached = WaypointIndex;
			WaypointIndex++;
		}
		else
		{
			const FFixedVector Direction = ToWaypoint.GetNormalized();
			CurrentPos = CurrentPos + Direction * RemainingBudget;
			RemainingBudget = FFixedPoint::Zero;
		}
	}

	Entity.Transform.SetLocation(CurrentPos);

	const FFixedPoint DistToDestSq = FFixedVector::DistSquared(CurrentPos, FinalDestination);
	if (DistToDestSq <= AcceptanceRadiusSq)
	{
		return true;
	}
	if (WaypointIndex >= WaypointCount)
	{
		return true;
	}
	return false;
}
