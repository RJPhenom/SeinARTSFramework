/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinEffect.h
 * @brief   Blueprintable base class for designer-authored sim-side effects.
 *          Per DESIGN §8: one primitive handles instant / finite / infinite /
 *          periodic, with `Scope` determining where the effect lives (Instance
 *          on the target entity, Archetype on the owner's player state,
 *          Player on the owner's player state targeting player-state fields).
 *
 *          Configuration lives on the CDO; `FSeinActiveEffect` in component
 *          storage carries only the class reference + per-instance runtime
 *          state (stacks, remaining duration, source, instance ID).
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinPlayerID.h"
#include "GameplayTagContainer.h"
#include "Attributes/SeinModifier.h"
#include "SeinEffect.generated.h"

/**
 * Where a tech effect lands on apply. Per DESIGN §10 Q5.
 * `Player` = researching player only. `Team` = every teammate (tracked via the
 * alliance graph). Non-tech effects leave this at the default and it's inert.
 */
UENUM(BlueprintType)
enum class ESeinTechScope : uint8
{
	/** Default. Applies only to the researching player. */
	Player,
	/** Applies to every teammate of the researching player. */
	Team
};

/**
 * How re-application of the same effect class to the same target behaves.
 * DESIGN §8 Q1d.
 */
UENUM(BlueprintType)
enum class ESeinEffectStackingRule : uint8
{
	/** Default. On re-apply, CurrentStacks++; modifiers multiply by stack count;
	 *  duration refreshes to the CDO's Duration. Cap at MaxStacks. */
	Stack,
	/** CurrentStacks stays at 1; duration refreshes to the CDO's Duration.
	 *  Classic "refresh DoT timer without stacking". */
	Refresh,
	/** Each apply adds a separate FSeinActiveEffect instance that tracks its own
	 *  timer. MaxStacks caps the number of concurrent independent instances. */
	Independent
};

/**
 * Designer-authored effect base. Subclass in Blueprint to define behavior.
 *
 * Effects are deterministic and run sim-side. Hooks are BlueprintImplementable —
 * designers fill them in BP graphs; C++ provides no default behavior.
 *
 * Duration semantics: `Zero` = instant (apply + remove same tick), negative
 * (sentinel `-1`) = infinite, positive = finite (ticks down in
 * `FSeinEffectTickSystem`).
 *
 * TickInterval semantics: `Zero` = no periodic firing, positive = fire
 * `OnTick` every N sim-seconds while active.
 */
UCLASS(Blueprintable, Abstract, ClassGroup = (SeinARTS), meta = (DisplayName = "SeinARTS Effect"))
class SEINARTSCOREENTITY_API USeinEffect : public UObject
{
	GENERATED_BODY()

public:
	// ─── Identity ───

	/** Gameplay tag that uniquely identifies this effect class. Used by
	 *  RemoveEffectsWithTag and for UI / replay hooks. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Effect")
	FGameplayTag EffectTag;

	// ─── Scope + Lifetime ───

	/** Where this effect lives + what its modifiers target. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Effect|Scope")
	ESeinModifierScope Scope = ESeinModifierScope::Instance;

	/** For Archetype-scope effects: the modifier's TargetArchetypeTag is usually
	 *  authored per-modifier, but this field lets a designer default every modifier
	 *  on this effect to a single archetype tag. Left unset = modifiers carry their
	 *  own per-entry TargetArchetypeTag. (Design convenience, not framework-enforced.) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Effect|Scope",
		meta = (EditCondition = "Scope == ESeinModifierScope::Archetype"))
	FGameplayTag DefaultTargetArchetypeTag;

	/** Zero = instant (apply + remove same tick). Negative (sentinel -1) = infinite.
	 *  Positive = finite; ticks decrement via FSeinEffectTickSystem. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Effect|Duration")
	FFixedPoint Duration = FFixedPoint::Zero;

	/** Zero = no periodic firing. Positive = fire OnTick every TickInterval sim-seconds
	 *  while active. Orthogonal to Duration: an effect can be finite + periodic
	 *  (DoT for 30s, 1 dmg/s) or infinite + periodic (aura fires every 5s forever). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Effect|Duration")
	FFixedPoint TickInterval = FFixedPoint::Zero;

	// ─── Stacking ───

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Effect|Stacking")
	ESeinEffectStackingRule StackingRule = ESeinEffectStackingRule::Stack;

	/** Cap for stack count (Stack / Independent rules). 1 = non-stacking.
	 *  Designers wanting effectively-unlimited stacks use a large number (e.g. 9999). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Effect|Stacking", meta = (ClampMin = "1"))
	int32 MaxStacks = 1;

	/** When the source entity dies, remove the effect. Default false — most effects
	 *  persist past source death (bleed from a dead attacker, tech modifier after
	 *  the researcher-building dies). Opt in for source-dependent effects. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Effect|Stacking")
	bool bRemoveOnSourceDeath = false;

	// ─── Modifiers + Tags ───

	/** Attribute modifiers applied while this effect is active. Scope comes from
	 *  the containing effect's Scope field — designers don't re-author it per modifier. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Effect|Modifiers")
	TArray<FSeinModifier> Modifiers;

	/** Tags granted to the target entity while this effect is active. Grants are
	 *  routed through USeinWorldSubsystem::GrantTag (refcount grant per DESIGN §2)
	 *  so overlapping grants from abilities / other effects survive this effect's
	 *  removal. Ignored for Player-scope effects (no player-level tag storage yet). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Effect|Tags")
	FGameplayTagContainer GrantedTags;

	/** On apply, remove any existing active effect on the same target whose CDO's
	 *  EffectTag matches any tag in this container. Classic "anti-poison removes poison". */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Effect|Tags")
	FGameplayTagContainer RemoveEffectsWithTag;

	// ─── Tech (DESIGN §10 — tech is an effect) ───

	/** Default false = permanent. True = can be removed post-apply via the standard
	 *  effect-remove API. Typical RTS baselines keep most tech permanent; revocable
	 *  supports commander-swap / era-regression patterns. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Effect|Tech")
	bool bRevocable = false;

	/** Per-tech apply scope. `Player` (default) applies to the researching player
	 *  only. `Team` applies to every teammate via the alliance graph. Inert for
	 *  Instance-scope effects (no player to fan out to). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Effect|Tech")
	ESeinTechScope TechScope = ESeinTechScope::Player;

	/** Tags that MUST be ABSENT from the target's tag set for this effect to be
	 *  researchable / applicable. Enables mutually-exclusive commander picks and
	 *  "you already picked Tier 2A so can't pick Tier 2B" patterns. DESIGN §10 Q6. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Effect|Tech")
	FGameplayTagContainer ForbiddenPrerequisiteTags;

	/** Escape hatch for BP-scripted eligibility logic (faction-specific, time-gated,
	 *  map-condition-based). Runs after tag prerequisite + forbidden checks; both
	 *  must pass for the research to be enqueue-able / apply-able. DESIGN §10 Q6. */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Effect|Tech", meta = (DisplayName = "Can Research"))
	bool CanResearch(FSeinPlayerID PlayerID) const;
	virtual bool CanResearch_Implementation(FSeinPlayerID PlayerID) const { return true; }

	// ─── BP hooks ───

	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Effect", meta = (DisplayName = "On Apply"))
	void OnApply(FSeinEntityHandle Target, FSeinEntityHandle Source);

	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Effect", meta = (DisplayName = "On Tick"))
	void OnTick(FSeinEntityHandle Target, FFixedPoint DeltaTime);

	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Effect", meta = (DisplayName = "On Expire"))
	void OnExpire(FSeinEntityHandle Target);

	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Effect", meta = (DisplayName = "On Removed"))
	void OnRemoved(FSeinEntityHandle Target, bool bByExpiration);
};
