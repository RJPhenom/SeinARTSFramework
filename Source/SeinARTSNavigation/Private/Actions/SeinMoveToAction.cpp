#include "Actions/SeinMoveToAction.h"
#include "SeinPathfinder.h"
#include "SeinNavigationSubsystem.h"
#include "SeinNavigationGrid.h"
#include "SeinFlowFieldPlanner.h"
#include "Grid/SeinAbstractGraph.h"
#include "Grid/SeinFlowFieldPlan.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinMovementData.h"
#include "Components/SeinTagData.h"
#include "Movement/SeinMovementProfile.h"
#include "Movement/SeinInfantryMovementProfile.h"
#include "Components/SeinMovementProfileComponent.h"
#include "Abilities/SeinMoveToProxy.h"
#include "Abilities/SeinAbility.h"
#include "Events/SeinVisualEvent.h"
#include "Engine/World.h"
#include "Types/Entity.h"

void USeinMoveToAction::Initialize(const FFixedVector& InDestination, USeinPathfinder* InPathfinder, FFixedPoint InAcceptanceRadius)
{
	Destination = InDestination;
	Pathfinder = InPathfinder;
	AcceptanceRadiusSq = InAcceptanceRadius * InAcceptanceRadius;
	CurrentWaypointIndex = 0;
	CurrentStepIndex = 0;
	Path.bIsValid = false;
	FlowPlan = FSeinFlowFieldPlan();
	KinState = FSeinMovementKinematicState();
}

bool USeinMoveToAction::TickAction(FFixedPoint DeltaTime, USeinWorldSubsystem& World)
{
	FSeinEntity* Entity = World.GetEntity(OwnerEntity);
	if (!Entity)
	{
		Fail(static_cast<uint8>(ESeinMoveFailureReason::EntityDestroyed));
		return true;
	}

	FSeinMovementData* MoveComp = World.GetComponent<FSeinMovementData>(OwnerEntity);
	if (!MoveComp)
	{
		Fail(static_cast<uint8>(ESeinMoveFailureReason::NoMovementComponent));
		return true;
	}

	// Resolve profile on first tick
	if (!ResolvedProfile)
	{
		UClass* ProfileClass = nullptr;
		if (const FSeinMovementProfileComponent* ProfileComp = World.GetComponent<FSeinMovementProfileComponent>(OwnerEntity))
		{
			ProfileClass = ProfileComp->Profile.Get();
		}
		if (!ProfileClass)
		{
			ProfileClass = USeinInfantryMovementProfile::StaticClass();
		}
		ResolvedProfile = NewObject<USeinMovementProfile>(this, ProfileClass);
	}

	// Build a flow-field plan on first tick (if a planner + grid are available).
	if (!FlowPlan.bValid && !Path.bIsValid)
	{
		UWorld* OuterWorld = GetWorld() ? GetWorld() : (World.GetWorld() ? World.GetWorld() : nullptr);
		USeinNavigationSubsystem* Nav = OuterWorld ? OuterWorld->GetSubsystem<USeinNavigationSubsystem>() : nullptr;
		USeinNavigationGrid* Grid = Nav ? Nav->GetGrid() : nullptr;
		USeinFlowFieldPlanner* Planner = Nav ? Nav->GetFlowFieldPlanner() : nullptr;

		if (Grid && Planner && Grid->Layers.Num() > 0)
		{
			const FSeinCellAddress StartAddr = Grid->ResolveCellAddress(Entity->Transform.GetLocation());
			const FSeinCellAddress GoalAddr = Grid->ResolveCellAddress(Destination);

			FGameplayTagContainer AgentTags;
			if (const FSeinTagData* TagComp = World.GetComponent<FSeinTagData>(OwnerEntity))
			{
				AgentTags = TagComp->CombinedTags;
			}

			const FGuid Key = USeinFlowFieldPlanner::KeyForCells(StartAddr, GoalAddr);
			FlowPlan = Planner->GetOrBuild(Key, StartAddr, GoalAddr, AgentTags);
			CurrentStepIndex = 0;
		}

		// Legacy A* fallback: if no planner available OR plan is invalid, fall back.
		if (!FlowPlan.bValid)
		{
			if (!Pathfinder)
			{
				Fail(static_cast<uint8>(ESeinMoveFailureReason::NoPathfinder));
				return true;
			}

			FSeinPathRequest Request;
			Request.Start = Entity->Transform.GetLocation();
			Request.End = Destination;
			Request.Requester = OwnerEntity;
			ResolvedProfile->BuildPath(Pathfinder, Request, Path);

			if (!Path.bIsValid || Path.GetWaypointCount() == 0)
			{
				Fail(static_cast<uint8>(ESeinMoveFailureReason::PathNotFound));
				return true;
			}
			CurrentWaypointIndex = 0;
		}
	}

	// Flow-plan-driven advancement.
	if (FlowPlan.bValid)
	{
		UWorld* OuterWorld = GetWorld() ? GetWorld() : (World.GetWorld() ? World.GetWorld() : nullptr);
		USeinNavigationSubsystem* Nav = OuterWorld ? OuterWorld->GetSubsystem<USeinNavigationSubsystem>() : nullptr;
		USeinNavigationGrid* Grid = Nav ? Nav->GetGrid() : nullptr;
		if (!Grid || !FlowPlan.Steps.IsValidIndex(CurrentStepIndex))
		{
			NotifyCompleted();
			return true;
		}

		const FSeinAbstractPathStep& Step = FlowPlan.Steps[CurrentStepIndex];

		// Navlink steps: deterministic inline traversal (designer visuals via the
		// link's TraversalAbility hook are deferred to Session 3.5 polish).
		if (Step.NavLinkID != INDEX_NONE && Grid->NavLinks.IsValidIndex(Step.NavLinkID))
		{
			const FSeinNavLinkRecord& Link = Grid->NavLinks[Step.NavLinkID];
			const FSeinCellAddress EndAddr =
				(Link.StartCell.Layer == Step.Layer) ? Link.EndCell : Link.StartCell;

			if (Grid->IsValidCellAddr(EndAddr))
			{
				const int32 LayerIdx = EndAddr.Layer;
				const FIntPoint XY = Grid->IndexToXY(EndAddr.CellIndex);
				const FFixedVector EndWorld = Grid->GridToWorldCenter(XY);
				FFixedVector FinalEndWorld = EndWorld;
				if (Grid->Layers.IsValidIndex(LayerIdx))
				{
					FinalEndWorld.Z = FFixedPoint::FromFloat(Grid->Layers[LayerIdx].GetCellWorldZ(EndAddr.CellIndex));
				}
				Entity->Transform.SetLocation(FinalEndWorld);
			}

			// Fire an AbilityActivated/Ended pair on the traversal ability tag so designers
			// can bind VFX / audio without blocking sim-side advancement.
			if (Link.TraversalAbility)
			{
				if (const USeinAbility* CDO = GetDefault<USeinAbility>(Link.TraversalAbility.Get()))
				{
					const FGameplayTag TraversalTag = CDO->AbilityTag;
					World.EnqueueVisualEvent(FSeinVisualEvent::MakeAbilityEvent(OwnerEntity, TraversalTag, /*bActivated=*/true));
					World.EnqueueVisualEvent(FSeinVisualEvent::MakeAbilityEvent(OwnerEntity, TraversalTag, /*bActivated=*/false));
				}
			}

			++CurrentStepIndex;
			if (!FlowPlan.Steps.IsValidIndex(CurrentStepIndex))
			{
				NotifyCompleted();
				return true;
			}
			return false;
		}

		if (!FlowPlan.PerClusterFields.IsValidIndex(Step.FlowFieldIndex))
		{
			NotifyCompleted();
			return true;
		}
		const FSeinClusterFlowField& Field = FlowPlan.PerClusterFields[Step.FlowFieldIndex];

		// Step goal in world space.
		const FIntPoint GoalXY = Grid->IndexToXY(Step.GoalCellIndex);
		FFixedVector StepGoalWorld = Grid->GridToWorldCenter(GoalXY);
		if (Grid->Layers.IsValidIndex(Step.Layer))
		{
			StepGoalWorld.Z = FFixedPoint::FromFloat(Grid->Layers[Step.Layer].GetCellWorldZ(Step.GoalCellIndex));
		}

		const bool bArrivedStep = ResolvedProfile->AdvanceViaFlowField(
			Grid, *Entity, *MoveComp, Field, StepGoalWorld, AcceptanceRadiusSq, DeltaTime, KinState);

		MoveComp->TargetLocation = StepGoalWorld;
		MoveComp->bHasTarget = true;

		if (bArrivedStep)
		{
			NotifyWaypointReached(CurrentStepIndex, FlowPlan.Steps.Num());
			++CurrentStepIndex;
			if (!FlowPlan.Steps.IsValidIndex(CurrentStepIndex))
			{
				MoveComp->bHasTarget = false;
				NotifyCompleted();
				return true;
			}
		}
		return false;
	}

	// Legacy A*-path advancement (back-compat fallback).
	int32 WaypointReached = -1;
	const bool bArrived = ResolvedProfile->AdvanceAlongPath(
		*Entity,
		*MoveComp,
		Path,
		CurrentWaypointIndex,
		Destination,
		AcceptanceRadiusSq,
		DeltaTime,
		KinState,
		WaypointReached);

	if (CurrentWaypointIndex < Path.GetWaypointCount())
	{
		MoveComp->TargetLocation = Path.Waypoints[CurrentWaypointIndex];
		MoveComp->bHasTarget = true;
	}
	else
	{
		MoveComp->bHasTarget = false;
	}

	if (WaypointReached >= 0)
	{
		NotifyWaypointReached(WaypointReached, Path.GetWaypointCount());
	}

	if (bArrived)
	{
		MoveComp->bHasTarget = false;
		NotifyCompleted();
		return true;
	}
	return false;
}

void USeinMoveToAction::OnCancel()
{
	if (Observer.IsValid())
	{
		Observer->NotifyCancelled();
	}
}

void USeinMoveToAction::OnFail(uint8 ReasonCode)
{
	if (Observer.IsValid())
	{
		Observer->NotifyFailed(static_cast<ESeinMoveFailureReason>(ReasonCode));
	}
}

void USeinMoveToAction::NotifyCompleted()
{
	if (Observer.IsValid())
	{
		Observer->NotifyCompleted();
	}
}

void USeinMoveToAction::NotifyWaypointReached(int32 Index, int32 Total)
{
	if (Observer.IsValid())
	{
		Observer->NotifyWaypointReached(Index, Total);
	}
}
