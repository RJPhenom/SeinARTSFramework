#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Core/SeinEntityHandle.h"
#include "SeinLatentAction.generated.h"

class USeinAbility;
class USeinWorldSubsystem;

/**
 * Base class for latent (multi-tick) ability nodes.
 * Latent actions run cooperatively on the sim thread — no OS threads involved.
 * Subclass and override TickAction to implement custom multi-frame behavior.
 */
UCLASS(Abstract)
class SEINARTSCOREENTITY_API USeinLatentAction : public UObject
{
	GENERATED_BODY()

public:
	/** The ability that owns this latent action */
	UPROPERTY()
	TObjectPtr<USeinAbility> OwningAbility;

	/** The entity that owns the ability */
	UPROPERTY()
	FSeinEntityHandle OwnerEntity;

	/** Whether this action has completed naturally */
	bool bCompleted = false;

	/** Whether this action was cancelled externally */
	bool bCancelled = false;

	/**
	 * Called each sim tick while the action is active.
	 * @return true when the action is complete and should be removed.
	 */
	virtual bool TickAction(FFixedPoint DeltaTime, USeinWorldSubsystem& World) { return true; }

	/** Called when the action is cancelled externally (e.g., ability cancelled) */
	virtual void OnCancel() {}

	/** Mark this action as completed */
	void Complete();

	/** Mark this action as cancelled */
	void Cancel();
};
