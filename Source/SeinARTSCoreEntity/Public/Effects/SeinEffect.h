/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinEffect.h
 * @brief   Blueprintable base class for designer-authored sim-side effects.
 *          See DESIGN.md §7. This is a minimal Phase-2-Session-2.3 shell —
 *          the full runtime wiring (CDO config, scope, stacking, modifier
 *          plumbing) lands when 2.3 implements the FSeinActiveEffect refactor.
 *          Today it provides a graph type so designers can author effect
 *          logic and so setter BPFLs can target it via RestrictedToClasses.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Core/SeinEntityHandle.h"
#include "GameplayTagContainer.h"
#include "SeinEffect.generated.h"

/**
 * Designer-authored effect base. Subclass in Blueprint to define behavior.
 *
 * Effects are deterministic and run sim-side. Hooks are BlueprintImplementable
 * — designers fill them in BP graphs; C++ provides no default behavior.
 *
 * Full per-effect configuration (modifiers, granted tags, duration, stacking,
 * scope) lands in Session 2.3 alongside the FSeinActiveEffect runtime refactor.
 */
UCLASS(Blueprintable, Abstract, ClassGroup = (SeinARTS), meta = (DisplayName = "SeinARTS Effect"))
class SEINARTSCOREENTITY_API USeinEffect : public UObject
{
	GENERATED_BODY()

public:
	/** Gameplay tag that uniquely identifies this effect type. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Effect")
	FGameplayTag EffectTag;

	/** Called once when the effect is applied to a target. */
	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Effect", meta = (DisplayName = "On Apply"))
	void OnApply(FSeinEntityHandle Target, FSeinEntityHandle Source);

	/** Called every TickInterval sim ticks while the effect is active.
	 *  (TickInterval gating arrives in Session 2.3; called every tick today.) */
	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Effect", meta = (DisplayName = "On Tick"))
	void OnTick(FSeinEntityHandle Target, FFixedPoint DeltaTime);

	/** Called when the effect's duration runs out (timed effects only). */
	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Effect", meta = (DisplayName = "On Expire"))
	void OnExpire(FSeinEntityHandle Target);

	/** Called when the effect is removed for any reason (including expiration). */
	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Effect", meta = (DisplayName = "On Removed"))
	void OnRemoved(FSeinEntityHandle Target, bool bByExpiration);
};
