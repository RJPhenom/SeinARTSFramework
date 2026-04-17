#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Core/SeinEntityHandle.h"
#include "GameplayTagContainer.h"
#include "Abilities/SeinAbilityTypes.h"
#include "SeinAbility.generated.h"

class USeinWorldSubsystem;

/**
 * Base class for all abilities in the SeinARTS framework.
 * Designers subclass this in Blueprint to define ability behavior.
 *
 * Abilities are deterministic and tick on the simulation side.
 * They support cooperative suspension via latent actions (no threads).
 */
UCLASS(Blueprintable, Abstract)
class SEINARTSCOREENTITY_API USeinAbility : public UObject
{
	GENERATED_BODY()

public:
	// ─── Configuration (set in BP class defaults) ───

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability")
	FText AbilityName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability")
	FGameplayTag AbilityTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability")
	FSeinAbilityRequirements Requirements;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability")
	FFixedPoint Cooldown;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability")
	TMap<FName, FFixedPoint> ResourceCost;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability")
	ESeinAbilityTargetType TargetType = ESeinAbilityTargetType::None;

	/** Generic tag container for identification, queries, and pattern matching. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability")
	FGameplayTagContainer QueryTags;

	/** Tags on entity that block this ability from activating */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability")
	FGameplayTagContainer ActivationBlockedTags;

	/** Passive abilities tick continuously without explicit activation */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability")
	bool bIsPassive = false;

	// ─── Runtime State (set by system) ───

	UPROPERTY(BlueprintReadOnly, Category = "Ability|Runtime")
	FSeinEntityHandle OwnerEntity;

	UPROPERTY(BlueprintReadOnly, Category = "Ability|Runtime")
	FSeinEntityHandle TargetEntity;

	UPROPERTY(BlueprintReadOnly, Category = "Ability|Runtime")
	FFixedVector TargetLocation;

	UPROPERTY(BlueprintReadOnly, Category = "Ability|Runtime")
	FFixedPoint CooldownRemaining;

	UPROPERTY(BlueprintReadOnly, Category = "Ability|Runtime")
	bool bIsActive = false;

	/** World subsystem reference (set on initialization) */
	UPROPERTY(Transient)
	TObjectPtr<USeinWorldSubsystem> WorldSubsystem;

	// ─── Lifecycle (Blueprint implementable) ───

	/** Override to add custom activation checks beyond tag requirements */
	UFUNCTION(BlueprintNativeEvent, Category = "Ability")
	bool CanActivate();
	virtual bool CanActivate_Implementation() { return true; }

	/** Called when the ability activates. Set up initial state here. */
	UFUNCTION(BlueprintNativeEvent, Category = "Ability")
	void OnActivate();
	virtual void OnActivate_Implementation() {}

	/** Called every sim tick while the ability is active */
	UFUNCTION(BlueprintNativeEvent, Category = "Ability")
	void OnTick(FFixedPoint DeltaTime);
	virtual void OnTick_Implementation(FFixedPoint DeltaTime) {}

	/** Called when the ability ends, either naturally or via cancellation */
	UFUNCTION(BlueprintNativeEvent, Category = "Ability")
	void OnEnd(bool bWasCancelled);
	virtual void OnEnd_Implementation(bool bWasCancelled) {}

	// ─── Control (callable from BP ability scripts) ───

	/** End this ability normally */
	UFUNCTION(BlueprintCallable, Category = "Ability")
	void EndAbility();

	/** Cancel this ability (forced termination) */
	UFUNCTION(BlueprintCallable, Category = "Ability")
	void CancelAbility();

	// ─── Internal ───

	void InitializeAbility(FSeinEntityHandle Owner, USeinWorldSubsystem* Subsystem);
	void ActivateAbility(FSeinEntityHandle Target, FFixedVector Location);
	void TickAbility(FFixedPoint DeltaTime);
	void DeactivateAbility(bool bCancelled);
	void TickCooldown(FFixedPoint DeltaTime);
	bool IsOnCooldown() const;
};
