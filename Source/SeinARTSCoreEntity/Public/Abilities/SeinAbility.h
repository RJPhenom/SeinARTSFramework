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

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability")
	FText AbilityName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability")
	FGameplayTag AbilityTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability")
	FSeinAbilityRequirements Requirements;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability")
	FFixedPoint Cooldown;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability")
	TMap<FName, FFixedPoint> ResourceCost;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability")
	ESeinAbilityTargetType TargetType = ESeinAbilityTargetType::None;

	/** Generic tag container for identification, queries, and pattern matching. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability")
	FGameplayTagContainer QueryTags;

	/** If the entity has any of these tags, this ability refuses to activate.
	 *  Combine with OwnedTags on the same ability to self-block (e.g., a grenade
	 *  that owns Ability.State.Channeling and blocks on the same tag cannot
	 *  reissue while a throw is in progress).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability")
	FGameplayTagContainer ActivationBlockedTags;

	/** Tags this ability grants to the entity while active. Granted on activate,
	 *  ungranted on deactivate. Tag presence is refcounted (DESIGN.md §2) —
	 *  overlapping grants from archetype BaseTags, other abilities, or effects
	 *  stay present until every source has released the tag.
	 *  Used with CancelAbilitiesWithTag for cross-ability arbitration.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability")
	FGameplayTagContainer OwnedTags;

	/** On activate, cancel any currently-active ability (including this one) whose
	 *  OwnedTags intersect this set. Including this ability's own OwnedTag here
	 *  gives you self-cancelling reissue (e.g., repeat-right-click Move).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability")
	FGameplayTagContainer CancelAbilitiesWithTag;

	/** Passive abilities tick continuously without explicit activation */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability")
	bool bIsPassive = false;

	// ─── Runtime State (set by system) ───

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Ability|Runtime")
	FSeinEntityHandle OwnerEntity;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Ability|Runtime")
	FSeinEntityHandle TargetEntity;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Ability|Runtime")
	FFixedVector TargetLocation;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Ability|Runtime")
	FFixedPoint CooldownRemaining;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Ability|Runtime")
	bool bIsActive = false;

	/** World subsystem reference (set on initialization) */
	UPROPERTY(Transient)
	TObjectPtr<USeinWorldSubsystem> WorldSubsystem;

	// ─── Lifecycle (Blueprint implementable) ───

	/** Override to add custom activation checks beyond tag requirements */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Ability")
	bool CanActivate();
	virtual bool CanActivate_Implementation() { return true; }

	/** Called when the ability activates. Set up initial state here. */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Ability")
	void OnActivate();
	virtual void OnActivate_Implementation() {}

	/** Called every sim tick while the ability is active */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Ability")
	void OnTick(FFixedPoint DeltaTime);
	virtual void OnTick_Implementation(FFixedPoint DeltaTime) {}

	/** Called when the ability ends, either naturally or via cancellation */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Ability")
	void OnEnd(bool bWasCancelled);
	virtual void OnEnd_Implementation(bool bWasCancelled) {}

	// ─── Control (callable from BP ability scripts) ───

	/** End this ability normally */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Ability")
	void EndAbility();

	/** Cancel this ability (forced termination) */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Ability")
	void CancelAbility();

	// ─── Internal ───

	void InitializeAbility(FSeinEntityHandle Owner, USeinWorldSubsystem* Subsystem);
	void ActivateAbility(FSeinEntityHandle Target, FFixedVector Location);
	void TickAbility(FFixedPoint DeltaTime);
	void DeactivateAbility(bool bCancelled);
	void TickCooldown(FFixedPoint DeltaTime);
	bool IsOnCooldown() const;
};
