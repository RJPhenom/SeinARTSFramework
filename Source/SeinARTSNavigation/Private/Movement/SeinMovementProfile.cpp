#include "Movement/SeinMovementProfile.h"
#include "SeinPathfinder.h"
#include "SeinPathTypes.h"
#include "SeinNavigationGrid.h"
#include "Grid/SeinFlowFieldPlan.h"
#include "Components/SeinMovementData.h"
#include "Types/Entity.h"

void USeinMovementProfile::BuildPath(USeinPathfinder* Pathfinder, const FSeinPathRequest& Request, FSeinPath& OutPath) const
{
	if (!Pathfinder)
	{
		OutPath.Clear();
		return;
	}
	OutPath = Pathfinder->FindPath(Request);
}

bool USeinMovementProfile::AdvanceViaFlowField(
	const USeinNavigationGrid* Grid,
	FSeinEntity& Entity,
	const FSeinMovementData& MoveComp,
	const FSeinClusterFlowField& Field,
	const FFixedVector& StepGoalWorld,
	FFixedPoint AcceptanceRadiusSq,
	FFixedPoint DeltaTime,
	FSeinMovementKinematicState& KinState) const
{
	// Straight-line seek toward the step goal. Subclasses (wheeled / tracked)
	// override for turn-rate / acceleration / formation offset behavior.
	const FFixedVector CurLoc = Entity.Transform.GetLocation();
	const FFixedVector Delta = StepGoalWorld - CurLoc;
	const FFixedPoint DistSq = FFixedVector::DotProduct(Delta, Delta);
	if (DistSq <= AcceptanceRadiusSq)
	{
		return true;
	}

	// Direction: prefer the flow field's direction for the cell we're currently in; fall
	// back to direct toward-goal if the field says unreachable.
	FFixedVector Direction = Delta;
	if (Grid && Field.TileExtent.X > 0 && Field.TileExtent.Y > 0 && Field.Directions.Num() > 0)
	{
		const FIntPoint CellXY = Grid->WorldToGrid(CurLoc);
		const int32 LX = CellXY.X - Field.TileOriginXY.X;
		const int32 LY = CellXY.Y - Field.TileOriginXY.Y;
		if (LX >= 0 && LX < Field.TileExtent.X && LY >= 0 && LY < Field.TileExtent.Y)
		{
			const int32 LIdx = LY * Field.TileExtent.X + LX;
			const uint8 Dir = Field.Directions.IsValidIndex(LIdx) ? Field.Directions[LIdx] : FSeinClusterFlowField::DIR_UNREACHABLE;
			if (Dir != FSeinClusterFlowField::DIR_UNREACHABLE)
			{
				// Direction codes: 0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW.
				static const int32 DX[8] = {  0,  1,  1,  1,  0, -1, -1, -1 };
				static const int32 DY[8] = { -1, -1,  0,  1,  1,  1,  0, -1 };
				const FFixedPoint FDX = FFixedPoint::FromInt(DX[Dir]);
				const FFixedPoint FDY = FFixedPoint::FromInt(DY[Dir]);
				Direction = FFixedVector(FDX, FDY, FFixedPoint::Zero);
			}
		}
	}

	// Normalize.
	const FFixedPoint Len = Direction.Size();
	if (Len > FFixedPoint::Zero)
	{
		Direction = Direction / Len;
	}

	// Move speed * delta along direction, but clamp at the step goal so we don't overshoot.
	const FFixedPoint StepDist = MoveComp.MoveSpeed * DeltaTime;
	const FFixedPoint GoalDist = Delta.Size();
	const FFixedPoint UseDist = (StepDist > GoalDist) ? GoalDist : StepDist;
	const FFixedVector NewLoc = CurLoc + (Direction * UseDist);
	Entity.Transform.SetLocation(NewLoc);
	KinState.CurrentSpeed = MoveComp.MoveSpeed;

	const FFixedVector NewDelta = StepGoalWorld - NewLoc;
	return FFixedVector::DotProduct(NewDelta, NewDelta) <= AcceptanceRadiusSq;
}
