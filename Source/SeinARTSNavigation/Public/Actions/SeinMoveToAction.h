#pragma once

#include "CoreMinimal.h"
#include "Abilities/SeinLatentAction.h"
#include "SeinPathTypes.h"
#include "SeinMoveToAction.generated.h"

class USeinPathfinder;

/**
 * Latent action that moves an entity along a pathfound route.
 * Requests a path on Initialize, then each tick advances the entity
 * along the waypoint chain at its FSeinMovementComponent::MoveSpeed.
 *
 * Completes when the entity reaches the final waypoint.
 * Completes immediately (as failure) if pathfinding fails.
 */
UCLASS()
class SEINARTSNAVIGATION_API USeinMoveToAction : public USeinLatentAction
{
	GENERATED_BODY()

public:
	/**
	 * Set up the move action.
	 * @param InDestination   World-space target position.
	 * @param InPathfinder    Pathfinder to use for route computation.
	 * @param InAcceptanceRadius  How close to the destination counts as "arrived".
	 */
	void Initialize(const FFixedVector& InDestination, USeinPathfinder* InPathfinder, FFixedPoint InAcceptanceRadius = FFixedPoint::FromInt(1));

	virtual bool TickAction(FFixedPoint DeltaTime, USeinWorldSubsystem& World) override;
	virtual void OnCancel() override;

	/** The computed path (empty if pathfinding failed). */
	UPROPERTY()
	FSeinPath Path;

	/** True if pathfinding succeeded and the entity is following waypoints. */
	bool IsPathValid() const { return Path.bIsValid; }

private:
	/** World-space destination. */
	FFixedVector Destination;

	/** Squared acceptance radius for arrival checks. */
	FFixedPoint AcceptanceRadiusSq;

	/** Current waypoint index the entity is moving toward. */
	int32 CurrentWaypointIndex = 0;

	/** Cached pathfinder reference. */
	UPROPERTY()
	TObjectPtr<USeinPathfinder> Pathfinder;
};
