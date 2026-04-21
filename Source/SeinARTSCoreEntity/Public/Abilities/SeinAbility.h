#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Core/SeinEntityHandle.h"
#include "GameplayTagContainer.h"
#include "Abilities/SeinAbilityTypes.h"
#include "Data/SeinResourceTypes.h"
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
	// ─── Identity ───

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability")
	FText AbilityName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability")
	FGameplayTag AbilityTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability")
	FSeinAbilityRequirements Requirements;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability")
	ESeinAbilityTargetType TargetType = ESeinAbilityTargetType::None;

	/** Generic tag container for identification, queries, and pattern matching. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability")
	FGameplayTagContainer QueryTags;

	/** Passive abilities tick continuously without explicit activation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability")
	bool bIsPassive = false;

	// ─── Cost + cooldown ───

	/** Resource cost to activate (unified cost struct per DESIGN §6). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Cost")
	FSeinResourceCost ResourceCost;

	/** Refund the deducted cost when the ability is cancelled. Default true — matches
	 *  typical RTS economy where cancelling "didn't happen" from the ledger's view.
	 *  Set false for punitive-cancel abilities. DESIGN §7 Q2a. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Cost")
	bool bRefundCostOnCancel = true;

	/** Cooldown duration in sim-seconds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Cost")
	FFixedPoint Cooldown;

	/** When the cooldown begins. DESIGN §7 Q4c. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Cost")
	ESeinCooldownStartTiming CooldownStartTiming = ESeinCooldownStartTiming::OnActivate;

	/** Reset the cooldown when the ability is cancelled. Default false — "you used it
	 *  recently" still applies to mid-use cancels. Designers opt in to true for
	 *  abilities where pre-commit cancel should be free (usually paired with
	 *  CooldownStartTiming == OnEnd). DESIGN §7 Q3c. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Cost")
	bool bRefundCooldownOnCancel = false;

	// ─── Target validation (declarative — DESIGN §7 Q5c/Q6) ───

	/** Maximum activation distance from owner to target (location or entity). Zero = unlimited. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Targeting")
	FFixedPoint MaxRange = FFixedPoint::Zero;

	/** Tag query the target entity must satisfy. Empty query = any target allowed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Targeting")
	FGameplayTagQuery ValidTargetTags;

	/** Require line-of-sight from owner to target. Integrates with §12 Vision — until
	 *  that lands the LOS check always passes. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Targeting")
	bool bRequiresLineOfSight = false;

	/** Require a nav-reachable target. When true, ProcessCommands::ActivateAbility
	 *  invokes USeinWorldSubsystem's registered pathable-target resolver (provided
	 *  by USeinNavigationSubsystem) before activation; on failure, the command is
	 *  rejected with SeinARTS.Command.Reject.PathUnreachable. Pre-reject skipped
	 *  if no resolver is registered (keeps tests + nav-less games working). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Targeting")
	bool bRequiresPathableTarget = false;

	/** What to do when activation is attempted with a target outside MaxRange. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Targeting")
	ESeinOutOfRangeBehavior OutOfRangeBehavior = ESeinOutOfRangeBehavior::Reject;

	// ─── Arbitration (DESIGN §3 + §7) ───

	/** If the entity has any of these tags, this ability refuses to activate.
	 *  Combine with OwnedTags on the same ability to self-block (e.g., a grenade
	 *  that owns Ability.State.Channeling and blocks on the same tag cannot
	 *  reissue while a throw is in progress).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Arbitration")
	FGameplayTagContainer ActivationBlockedTags;

	/** Tags this ability grants to the entity while active. Granted on activate,
	 *  ungranted on deactivate. Tag presence is refcounted (DESIGN.md §2) —
	 *  overlapping grants from archetype BaseTags, other abilities, or effects
	 *  stay present until every source has released the tag.
	 *  Used with CancelAbilitiesWithTag for cross-ability arbitration.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Arbitration")
	FGameplayTagContainer OwnedTags;

	/** On activate, cancel any currently-active ability (including this one) whose
	 *  OwnedTags intersect this set. Including this ability's own OwnedTag here
	 *  gives you self-cancelling reissue (e.g., repeat-right-click Move).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Ability|Arbitration")
	FGameplayTagContainer CancelAbilitiesWithTag;

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

	/** Snapshot of the cost actually deducted on activation. Empty unless currently
	 *  active. Used to drive refund-on-cancel without re-resolving cost at deactivate time. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Ability|Runtime")
	FSeinResourceCost DeductedCost;

	/** Whether the cooldown has been started for the current activation. Drives
	 *  the bRefundCooldownOnCancel + CooldownStartTiming == OnEnd interaction. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Ability|Runtime")
	bool bCooldownStarted = false;

	/** World subsystem reference (set on initialization) */
	UPROPERTY(Transient)
	TObjectPtr<USeinWorldSubsystem> WorldSubsystem;

	// ─── Lifecycle (Blueprint implementable) ───

	/** Override to add custom activation checks beyond the declarative target
	 *  validation (range / ValidTargetTags / LOS). Run after declarative checks. */
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

	/** Stamp the cost snapshot on activation. Called by ProcessCommands after
	 *  a successful USeinResourceBPFL::SeinDeduct. */
	void RecordDeductedCost(const FSeinResourceCost& Cost) { DeductedCost = Cost; }
};
