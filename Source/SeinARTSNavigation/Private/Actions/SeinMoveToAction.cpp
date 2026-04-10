#include "Actions/SeinMoveToAction.h"
#include "SeinPathfinder.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinComponents.h"

void USeinMoveToAction::Initialize(const FFixedVector& InDestination, USeinPathfinder* InPathfinder, FFixedPoint InAcceptanceRadius)
{
	Destination = InDestination;
	Pathfinder = InPathfinder;
	AcceptanceRadiusSq = InAcceptanceRadius * InAcceptanceRadius;
	CurrentWaypointIndex = 0;

	if (!Pathfinder)
	{
		// No pathfinder — complete immediately as failure
		Path.bIsValid = false;
		return;
	}

	// Get entity current position for the path start
	// Note: OwnerEntity is set by the ability system before Initialize is called
	// We defer the actual path request to the first TickAction so the World ref is available
	Path.bIsValid = false;
}

bool USeinMoveToAction::TickAction(FFixedPoint DeltaTime, USeinWorldSubsystem& World)
{
	// Request path on first tick (World reference available here)
	if (!Path.bIsValid && Pathfinder)
	{
		FSeinEntity* Entity = World.GetEntity(OwnerEntity);
		if (!Entity)
		{
			return true; // Entity gone — complete
		}

		FSeinPathRequest Request;
		Request.Start = Entity->Transform.GetLocation();
		Request.End = Destination;
		Request.Requester = OwnerEntity;

		Path = Pathfinder->FindPath(Request);

		if (!Path.bIsValid || Path.GetWaypointCount() == 0)
		{
			return true; // No valid path — complete as failure
		}

		// Skip waypoint 0 if it's the start position
		CurrentWaypointIndex = 0;
	}

	// Get entity and movement component
	FSeinEntity* Entity = World.GetEntity(OwnerEntity);
	if (!Entity)
	{
		return true; // Entity destroyed mid-move
	}

	FSeinMovementComponent* MoveComp = World.GetComponent<FSeinMovementComponent>(OwnerEntity);
	if (!MoveComp)
	{
		return true; // No movement component — can't move
	}

	const FFixedPoint Speed = MoveComp->MoveSpeed;
	FFixedPoint RemainingBudget = Speed * DeltaTime;
	FFixedVector CurrentPos = Entity->Transform.GetLocation();

	// Advance along waypoints, consuming movement budget
	while (CurrentWaypointIndex < Path.GetWaypointCount() && RemainingBudget > FFixedPoint::Zero)
	{
		const FFixedVector& Waypoint = Path.Waypoints[CurrentWaypointIndex];
		const FFixedVector ToWaypoint = Waypoint - CurrentPos;
		const FFixedPoint DistSq = ToWaypoint.SizeSquared();

		// If within step distance of this waypoint, snap and advance
		const FFixedPoint BudgetSq = RemainingBudget * RemainingBudget;
		if (DistSq <= BudgetSq)
		{
			// Consume the distance to this waypoint
			const FFixedPoint Dist = ToWaypoint.Size();
			RemainingBudget = RemainingBudget - Dist;
			CurrentPos = Waypoint;
			CurrentWaypointIndex++;
		}
		else
		{
			// Move toward the waypoint by remaining budget
			const FFixedVector Direction = ToWaypoint.GetNormalized();
			CurrentPos = CurrentPos + Direction * RemainingBudget;
			RemainingBudget = FFixedPoint::Zero;
		}
	}

	// Write position back to entity
	Entity->Transform.SetLocation(CurrentPos);

	// Update the movement component target for visual/steering systems
	if (CurrentWaypointIndex < Path.GetWaypointCount())
	{
		MoveComp->TargetLocation = Path.Waypoints[CurrentWaypointIndex];
		MoveComp->bHasTarget = true;
	}
	else
	{
		MoveComp->bHasTarget = false;
	}

	// Check arrival at final destination
	const FFixedPoint DistToDestSq = FFixedVector::DistSquared(CurrentPos, Destination);
	if (DistToDestSq <= AcceptanceRadiusSq)
	{
		MoveComp->bHasTarget = false;
		return true; // Arrived
	}

	// Also complete if we've consumed all waypoints
	if (CurrentWaypointIndex >= Path.GetWaypointCount())
	{
		MoveComp->bHasTarget = false;
		return true; // Path exhausted
	}

	return false; // Still moving
}

void USeinMoveToAction::OnCancel()
{
	// We don't have a World reference in OnCancel, so just mark no target.
	// The movement component will be cleared on the next system tick
	// since the action is no longer active.
}
