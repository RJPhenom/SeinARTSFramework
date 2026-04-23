/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMoveToAction.h
 * @brief   Latent action that moves a sim entity along a USeinNavigation-
 *          produced path. Implementation-agnostic: the action never touches
 *          grids, pathfinders, or A* internals — it only consumes FSeinPath.
 *
 *          Kinematics are read from FSeinMovementData (MoveSpeed / Acceleration
 *          / TurnRate). Steering is minimal: seek toward next waypoint with an
 *          arrive radius at the final waypoint.
 */

#pragma once

#include "CoreMinimal.h"
#include "Abilities/SeinLatentAction.h"
#include "SeinPathTypes.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinMoveToAction.generated.h"

class USeinMoveToProxy;

/** Reasons a move can fail. Passed via USeinLatentAction::Fail() reason code. */
UENUM(BlueprintType)
enum class ESeinMoveFailureReason : uint8
{
	None                UMETA(DisplayName = "None"),
	PathNotFound        UMETA(DisplayName = "Path Not Found"),
	EntityDestroyed     UMETA(DisplayName = "Entity Destroyed"),
	NoMovementComponent UMETA(DisplayName = "No Movement Component"),
	NoNavigation        UMETA(DisplayName = "No Navigation"),
	Cancelled           UMETA(DisplayName = "Cancelled")
};

UCLASS()
class SEINARTSNAVIGATION_API USeinMoveToAction : public USeinLatentAction
{
	GENERATED_BODY()

public:

	void Initialize(const FFixedVector& InDestination, FFixedPoint InAcceptanceRadius = FFixedPoint::One);

	virtual bool TickAction(FFixedPoint DeltaTime, USeinWorldSubsystem& World) override;
	virtual void OnCancel() override;
	virtual void OnFail(uint8 ReasonCode) override;

	/** Optional observer — receives Completed/Failed/Waypoint/Cancelled events. */
	TWeakObjectPtr<USeinMoveToProxy> Observer;

	UPROPERTY()
	FSeinPath Path;

	bool IsPathValid() const { return Path.bIsValid; }

	/** Index of the waypoint the entity is currently heading toward. Public so
	 *  debug rendering can draw "entity → current waypoint → remaining path". */
	int32 GetCurrentWaypointIndex() const { return CurrentWaypointIndex; }

private:

	FFixedVector Destination;
	FFixedPoint AcceptanceRadiusSq;

	int32 CurrentWaypointIndex = 0;
	bool bPathResolved = false;

	void NotifyCompleted();
	void NotifyWaypointReached(int32 Index, int32 Total);
};
