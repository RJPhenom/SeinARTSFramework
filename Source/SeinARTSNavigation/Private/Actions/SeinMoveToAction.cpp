/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMoveToAction.cpp
 */

#include "Actions/SeinMoveToAction.h"
#include "Abilities/SeinMoveToProxy.h"
#include "SeinNavigation.h"
#include "SeinNavigationSubsystem.h"
#include "Movement/SeinLocomotion.h"
#include "Movement/SeinBasicMovement.h"

#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinMovementData.h"
#include "Types/Entity.h"
#include "Types/FixedPoint.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinMove, Log, All);

void USeinMoveToAction::Initialize(const FFixedVector& InDestination)
{
	Destination = InDestination;
	// Squaring deferred — resolved on first TickAction once MoveData is available.
	AcceptanceRadiusSq = FFixedPoint::Zero;
	CurrentWaypointIndex = 0;
	bPathResolved = false;
	TimeSinceLastRepath = FFixedPoint::Zero;
	Path.Clear();
	Locomotion = nullptr;
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

	USeinNavigation* Nav = USeinNavigationSubsystem::GetNavigationForWorld(&World);

	// First-tick setup: resolve path + instantiate the locomotion.
	if (!bPathResolved)
	{
		if (!Nav)
		{
			Fail(static_cast<uint8>(ESeinMoveFailureReason::NoNavigation));
			return true;
		}

		FSeinPathRequest Req;
		Req.Start = Entity->Transform.GetLocation();
		Req.End = Destination;
		Req.Requester = OwnerEntity;
		Req.AgentNavLayerMask = MoveComp->NavLayerMask;

		if (!Nav->FindPath(Req, Path) || Path.Waypoints.Num() == 0)
		{
			Fail(static_cast<uint8>(ESeinMoveFailureReason::PathNotFound));
			return true;
		}
		bPathResolved = true;
		CurrentWaypointIndex = 0;

		// Resolve acceptance radius from the unit's movement data — properly
		// a per-unit characteristic (footprint / turn radius), not a per-call
		// concern. Designers tune it on the component.
		AcceptanceRadiusSq = MoveComp->AcceptanceRadius * MoveComp->AcceptanceRadius;

		// Resolve the locomotion class. Soft class path on FSeinMovementData
		// decouples CoreEntity from SeinARTSNavigation (where the locomotion
		// classes live); TryLoadClass pulls in the resolved UClass* at first use.
		UClass* LocomotionClass = MoveComp->LocomotionClass.IsValid()
			? MoveComp->LocomotionClass.TryLoadClass<USeinLocomotion>()
			: nullptr;
		if (!LocomotionClass || LocomotionClass->HasAnyClassFlags(CLASS_Abstract))
		{
			LocomotionClass = USeinBasicMovement::StaticClass();
		}
		Locomotion = NewObject<USeinLocomotion>(this, LocomotionClass);
		if (Locomotion)
		{
			FSeinLocomotionContext BeginCtx{
				*Entity,
				*MoveComp,
				Path,
				CurrentWaypointIndex,
				AcceptanceRadiusSq,
				DeltaTime,
				Nav,
				&World,
				OwnerEntity
			};
			Locomotion->OnMoveBegin(BeginCtx);
		}
	}

	if (!Locomotion)
	{
		// Defensive: should never happen post-bPathResolved, but fail cleanly
		// rather than crash.
		Fail(static_cast<uint8>(ESeinMoveFailureReason::NoMovementComponent));
		return true;
	}

	// Repath check — runs BEFORE the locomotion tick so a fresh path takes
	// effect this same frame. See ESeinRepathMode.
	TimeSinceLastRepath = TimeSinceLastRepath + DeltaTime;
	switch (MoveComp->RepathMode)
	{
	case ESeinRepathMode::Interval:
	{
		const FFixedPoint Interval = (MoveComp->RepathInterval > FFixedPoint::Zero)
			? MoveComp->RepathInterval : FFixedPoint::FromInt(1) / FFixedPoint::FromInt(4);
		if (TimeSinceLastRepath >= Interval && Nav)
		{
			FSeinPathRequest Req;
			Req.Start = Entity->Transform.GetLocation();
			Req.End = Destination;
			Req.Requester = OwnerEntity;
			Req.AgentNavLayerMask = MoveComp->NavLayerMask;

			FSeinPath NewPath;
			if (Nav->FindPath(Req, NewPath) && NewPath.Waypoints.Num() > 0)
			{
				// Swap in the fresh path and reset the waypoint cursor —
				// NewPath.Waypoints[0] is offset from current position so
				// starting at 0 means "head to the next intended carrot."
				// Locomotion's CurrentSpeed (on MoveData) is preserved so
				// the unit doesn't lose momentum at the swap.
				Path = NewPath;
				CurrentWaypointIndex = 0;
				UE_LOG(LogSeinMove, Verbose,
					TEXT("Repath (Interval): %d new waypoints from (%.1f,%.1f)"),
					NewPath.Waypoints.Num(),
					Entity->Transform.GetLocation().X.ToFloat(),
					Entity->Transform.GetLocation().Y.ToFloat());
			}
			// FindPath failure: keep the previous path (best-effort) and
			// hope the next interval has better luck. Don't fail the action.
			TimeSinceLastRepath = FFixedPoint::Zero;
		}
		break;
	}

	case ESeinRepathMode::OffPathOnly:
		// TODO: implement off-path detection — compare AgentPos against the
		// current path polyline, repath when distance exceeds a threshold.
		// Currently a no-op (effectively "no repath").
		break;
	}

	// Delegate the per-tick advance. Locomotion mutates Entity.Transform and
	// CurrentWaypointIndex; returns true when the final waypoint is reached.
	const int32 PrevWaypoint = CurrentWaypointIndex;
	FSeinLocomotionContext TickCtx{
		*Entity,
		*MoveComp,
		Path,
		CurrentWaypointIndex,
		AcceptanceRadiusSq,
		DeltaTime,
		Nav,
		&World,
		OwnerEntity
	};
	const bool bReachedEnd = Locomotion->Tick(TickCtx);

	// Under-reports if the locomotion consumed multiple waypoints in one tick
	// (only the latest advance fires the notify). Acceptable for MVP; if
	// per-step granularity ever matters, swap to a TFunctionRef callback.
	if (CurrentWaypointIndex > PrevWaypoint)
	{
		NotifyWaypointReached(CurrentWaypointIndex - 1, Path.Waypoints.Num());
	}

	if (bReachedEnd)
	{
		Locomotion->OnMoveEnd(*Entity);
		NotifyCompleted();
		Complete();
		return true;
	}
	return false;
}

void USeinMoveToAction::OnCancel()
{
	if (USeinMoveToProxy* Proxy = Observer.Get())
	{
		Proxy->NotifyCancelled();
	}
}

void USeinMoveToAction::OnFail(uint8 ReasonCode)
{
	if (USeinMoveToProxy* Proxy = Observer.Get())
	{
		Proxy->NotifyFailed(static_cast<ESeinMoveFailureReason>(ReasonCode));
	}
}

void USeinMoveToAction::NotifyCompleted()
{
	if (USeinMoveToProxy* Proxy = Observer.Get())
	{
		Proxy->NotifyCompleted();
	}
}

void USeinMoveToAction::NotifyWaypointReached(int32 Index, int32 Total)
{
	if (USeinMoveToProxy* Proxy = Observer.Get())
	{
		Proxy->NotifyWaypointReached(Index, Total);
	}
}
