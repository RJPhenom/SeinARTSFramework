/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMoveToProxy.h
 * @brief   Blueprint async action node — the framework's AIMoveTo equivalent.
 *
 * Usage in an ability BP graph:
 *   [OnActivate] -> [Sein Move To (Dest)]
 *                       |- Completed       -> EndAbility
 *                       |- Failed (Reason) -> handle failure
 *                       |- WaypointReached -> play step SFX, etc.
 *                       |- Cancelled       -> cleanup
 *
 * Acceptance radius is sourced from the unit's
 * `FSeinMovementData::AcceptanceRadius` — a footprint/turn-radius property
 * of the unit, not the call site. Tune it on the movement component, not
 * here.
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

	/** Move the ability's owning entity to Destination using its movement
	 *  profile. Acceptance radius is read from the unit's
	 *  `FSeinMovementData::AcceptanceRadius`. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Ability|Movement",
	          meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "Ability",
	                  DisplayName = "Move To"))
	static USeinMoveToProxy* SeinMoveTo(
		USeinAbility* Ability,
		FFixedVector Destination);

	virtual void Activate() override;

	// Observer callbacks invoked by USeinMoveToAction
	void NotifyCompleted();
	void NotifyFailed(ESeinMoveFailureReason Reason);
	void NotifyWaypointReached(int32 Index, int32 Total);
	void NotifyCancelled();

private:
	UPROPERTY() TObjectPtr<USeinAbility> CachedAbility;
	FFixedVector CachedDestination;

	UPROPERTY() TObjectPtr<USeinMoveToAction> RunningAction;

	void BroadcastFailure(ESeinMoveFailureReason Reason);
};
