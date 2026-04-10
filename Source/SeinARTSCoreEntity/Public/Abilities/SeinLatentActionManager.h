#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Core/SeinEntityHandle.h"
#include "SeinLatentActionManager.generated.h"

class USeinLatentAction;
class USeinAbility;
class USeinWorldSubsystem;

/**
 * Manages all active latent actions in the simulation.
 * Ticked during the AbilityExecution phase of the sim loop.
 * Provides entity-level and ability-level cancellation.
 */
UCLASS()
class SEINARTSCOREENTITY_API USeinLatentActionManager : public UObject
{
	GENERATED_BODY()

public:
	/** Register a new latent action to be ticked */
	void RegisterAction(USeinLatentAction* Action);

	/** Tick all active actions and clean up completed ones */
	void TickAll(FFixedPoint DeltaTime, USeinWorldSubsystem& World);

	/** Cancel all latent actions belonging to the given entity */
	void CancelActionsForEntity(FSeinEntityHandle Handle);

	/** Cancel all latent actions belonging to the given ability */
	void CancelActionsForAbility(USeinAbility* Ability);

	/** Remove completed and cancelled actions from the active list */
	void CleanupCompleted();

	/** Get the number of currently active actions */
	int32 GetActiveActionCount() const;

private:
	UPROPERTY()
	TArray<TObjectPtr<USeinLatentAction>> ActiveActions;
};
