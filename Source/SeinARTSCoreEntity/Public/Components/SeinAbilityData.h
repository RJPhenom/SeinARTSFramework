#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "Abilities/SeinAbility.h"
#include "Abilities/SeinAbilityTypes.h"
#include "Components/SeinComponent.h"
#include "SeinAbilityData.generated.h"

class USeinWorldSubsystem;

/**
 * Component tracking an entity's abilities.
 *
 * Holds granted ability classes (from archetype authoring), runtime instances,
 * active primary + passive tracking, and — per DESIGN §7 Q9 — the command
 * resolver (DefaultCommands + FallbackAbilityTag). Designers author command
 * mappings here rather than on the archetype definition; a single edit surface
 * for "which ability fires for which input context."
 *
 * Phase 4 architecture: per DESIGN.md §2, component data is pure — no live
 * UObject refs. Ability instances are stored as `int32` IDs into a pool managed
 * by `USeinWorldSubsystem`. This makes:
 *   - State hash deterministic across processes (int32 IDs, not pointer values)
 *   - World snapshots portable (IDs survive disk/wire round-trips)
 *   - Reflection-driven hashing covers everything (no manual GetTypeHash needed)
 *
 * Pool registration happens in `USeinWorldSubsystem::InitializeEntityAbilities`;
 * unregister + free fires when the entity is destroyed. Walk sites use the
 * `Get*` accessors below (which take a `USeinWorldSubsystem&` so the pool
 * lookup happens at the call site rather than caching a pointer here).
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinAbilityData : public FSeinComponent
{
	GENERATED_BODY()

	/** Ability classes granted to this entity (set from archetype) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Ability")
	TArray<TSubclassOf<USeinAbility>> GrantedAbilityClasses;

	/** Pool IDs for runtime ability instances. Indices into
	 *  `USeinWorldSubsystem::AbilityPool`. INDEX_NONE = no ability. */
	UPROPERTY()
	TArray<int32> AbilityInstanceIDs;

	/** Pool ID of the currently-active primary ability (not passive).
	 *  INDEX_NONE = nothing active. */
	UPROPERTY()
	int32 ActiveAbilityID = INDEX_NONE;

	/** Pool IDs for currently-running passive abilities. */
	UPROPERTY()
	TArray<int32> ActivePassiveIDs;

	/**
	 * Default command mappings for this entity. Maps input contexts (tag sets) to
	 * ability tags. The player controller's smart-command resolver runs these to
	 * pick which ability to activate on right-click. Sorted by priority at resolve
	 * time — highest priority match wins.
	 *
	 * See FSeinCommandMapping for detailed usage examples.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Ability")
	TArray<FSeinCommandMapping> DefaultCommands;

	/**
	 * Fallback ability tag when no command mapping matches. Typically
	 * `SeinARTS.Ability.Move` so unmapped contexts default to move.
	 * If empty, no command is issued for unmatched contexts.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Ability")
	FGameplayTag FallbackAbilityTag;

	// ========== Accessors (pool lookup helpers) ==========

	/** Resolve `ActiveAbilityID` to a live ability ref via the world's pool.
	 *  Returns null on INDEX_NONE / unregistered ID / null world. */
	USeinAbility* GetActiveAbility(const USeinWorldSubsystem& World) const;

	/** Materialize the live ability instance array from `AbilityInstanceIDs`.
	 *  Result is sized + filled in iteration order; null entries indicate
	 *  freed pool slots (rare; typically the whole component is removed
	 *  before slot release). Hot-path callers should iterate IDs + call
	 *  `World.GetAbilityInstance(ID)` directly to avoid the array copy. */
	TArray<USeinAbility*> GetAbilityInstances(const USeinWorldSubsystem& World) const;

	/** Same shape for active passives. */
	TArray<USeinAbility*> GetActivePassives(const USeinWorldSubsystem& World) const;

	/** Find an ability instance by its AbilityTag. Walks `AbilityInstanceIDs`,
	 *  resolving each ID through the pool. */
	USeinAbility* FindAbilityByTag(const USeinWorldSubsystem& World, const FGameplayTag& Tag) const;

	/** Check whether this component has an ability with the given tag. */
	bool HasAbilityWithTag(const USeinWorldSubsystem& World, const FGameplayTag& Tag) const;

	/**
	 * Resolve a command context to an ability tag. Walks DefaultCommands in
	 * priority order; returns FallbackAbilityTag if no mapping's RequiredContext
	 * is satisfied. Pure data lookup — doesn't need the world.
	 */
	FGameplayTag ResolveCommandContext(const FGameplayTagContainer& Context) const;
};
