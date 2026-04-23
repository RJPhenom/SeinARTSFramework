/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMoveToProxy.h
 * @brief   Blueprint async action node — the framework's AIMoveTo equivalent.
 *
 * Usage in an ability BP graph:
 *   [OnActivate] -> [Sein Move To (Dest, Radius)]
 *                       |- Completed       -> EndAbility
 *                       |- Failed (Reason) -> handle failure
 *                       |- WaypointReached -> play step SFX, etc.
 *                       |- Cancelled       -> cleanup
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Actions/SeinMoveToAction.h"
#include "SeinMoveToProxy.generated.h"

class USeinAbility;
class USeinMoveToAction;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSeinMoveToSimpleDelegate);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSeinMoveToFailedDelegate, ESeinMoveFailureReason, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSeinMoveToWaypointDelegate, int32, WaypointIndex, int32, TotalWaypoints);

UCLASS()
class SEINARTSNAVIGATION_API USeinMoveToProxy : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable) FSeinMoveToSimpleDelegate   OnCompleted;
	UPROPERTY(BlueprintAssignable) FSeinMoveToFailedDelegate   OnFailed;
	UPROPERTY(BlueprintAssignable) FSeinMoveToWaypointDelegate OnWaypointReached;
	UPROPERTY(BlueprintAssignable) FSeinMoveToSimpleDelegate   OnCancelled;

	/** Move the ability's owning entity to Destination using its movement profile.
	 *  AcceptanceRadius is in world units (cm under UE's convention). The action
	 *  clamps it at runtime to >= half the nav-grid's cell size — values smaller
	 *  than that are practically impossible to reach (the unit's per-tick step
	 *  is bigger than the radius), so they get bumped with a warning. Reasonable
	 *  starting point for most grids is around half a cell; designers should set
	 *  this per ability based on the unit's footprint and how forgiving the
	 *  "arrived" semantics should feel. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Ability|Movement",
	          meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "Ability",
	                  DisplayName = "Move To"))
	static USeinMoveToProxy* SeinMoveTo(
		USeinAbility* Ability,
		FFixedVector Destination,
		FFixedPoint AcceptanceRadius);

	virtual void Activate() override;

	// Observer callbacks invoked by USeinMoveToAction
	void NotifyCompleted();
	void NotifyFailed(ESeinMoveFailureReason Reason);
	void NotifyWaypointReached(int32 Index, int32 Total);
	void NotifyCancelled();

private:
	UPROPERTY() TObjectPtr<USeinAbility> CachedAbility;
	FFixedVector CachedDestination;
	FFixedPoint  CachedAcceptanceRadius;

	UPROPERTY() TObjectPtr<USeinMoveToAction> RunningAction;

	void BroadcastFailure(ESeinMoveFailureReason Reason);
};
