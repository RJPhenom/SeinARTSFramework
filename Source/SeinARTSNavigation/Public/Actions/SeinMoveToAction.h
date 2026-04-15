#pragma once

#include "CoreMinimal.h"
#include "Abilities/SeinLatentAction.h"
#include "SeinPathTypes.h"
#include "Movement/SeinMovementKinematicState.h"
#include "SeinMoveToAction.generated.h"

class USeinPathfinder;
class USeinMovementProfile;
class USeinMoveToProxy;

/** Reasons a move can fail. Passed via USeinLatentAction::Fail() reason code. */
UENUM(BlueprintType)
enum class ESeinMoveFailureReason : uint8
{
	None                 UMETA(DisplayName = "None"),
	PathNotFound         UMETA(DisplayName = "Path Not Found"),
	EntityDestroyed      UMETA(DisplayName = "Entity Destroyed"),
	NoMovementComponent  UMETA(DisplayName = "No Movement Component"),
	NoPathfinder         UMETA(DisplayName = "No Pathfinder"),
	Cancelled            UMETA(DisplayName = "Cancelled")
};

/**
 * Latent action that moves an entity along a pathfound route.
 * Delegates path construction + path advancement to a USeinMovementProfile
 * (Infantry / Tracked / Wheeled). Reports completion/failure/waypoint/cancel
 * events to an optional observer (USeinMoveToProxy).
 */
UCLASS()
class SEINARTSNAVIGATION_API USeinMoveToAction : public USeinLatentAction
{
	GENERATED_BODY()

public:
	void Initialize(const FFixedVector& InDestination, USeinPathfinder* InPathfinder, FFixedPoint InAcceptanceRadius = FFixedPoint::One);

	virtual bool TickAction(FFixedPoint DeltaTime, USeinWorldSubsystem& World) override;
	virtual void OnCancel() override;
	virtual void OnFail(uint8 ReasonCode) override;

	/** Observer proxy (optional) — receives Completed/Failed/Waypoint/Cancelled callbacks. */
	TWeakObjectPtr<USeinMoveToProxy> Observer;

	UPROPERTY()
	FSeinPath Path;

	bool IsPathValid() const { return Path.bIsValid; }

private:
	FFixedVector Destination;
	FFixedPoint AcceptanceRadiusSq;
	int32 CurrentWaypointIndex = 0;

	UPROPERTY()
	TObjectPtr<USeinPathfinder> Pathfinder;

	UPROPERTY()
	TObjectPtr<USeinMovementProfile> ResolvedProfile;

	FSeinMovementKinematicState KinState;

	void NotifyCompleted();
	void NotifyWaypointReached(int32 Index, int32 Total);
};
