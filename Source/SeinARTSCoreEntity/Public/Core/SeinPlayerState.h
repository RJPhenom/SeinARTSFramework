/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinPlayerState.h
 * @brief   Sim-side player data (NOT APlayerState). Tracks faction, team,
 *          resources, and elimination status for a single player slot.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinPlayerID.h"
#include "Core/SeinFactionID.h"
#include "Types/FixedPoint.h"
#include "GameplayTagContainer.h"
#include "Attributes/SeinModifier.h"
#include "SeinPlayerState.generated.h"

/**
 * Sim-side player state. One per player slot in the lockstep simulation.
 * Holds faction assignment, team, named resources, and elimination flag.
 */
USTRUCT(BlueprintType)
struct SEINARTSCOREENTITY_API FSeinPlayerState
{
	GENERATED_BODY()

	/** Unique player identifier */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Player")
	FSeinPlayerID PlayerID;

	/** Faction this player is playing as */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Player")
	FSeinFactionID FactionID;

	/** Team index for alliance grouping (0 = no team / FFA) */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Player")
	uint8 TeamID = 0;

	/**
	 * Named resources (designer-defined: "Manpower", "Fuel", etc.).
	 * Keys are resource names, values are fixed-point amounts.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Economy")
	TMap<FName, FFixedPoint> Resources;

	/** Whether this player has been eliminated from the match */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Player")
	bool bEliminated = false;

	/** Whether this player has signalled ready (pre-game) */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Player")
	bool bReady = false;

	// --- Tech / Research ---

	/** Tech tags this player has unlocked through research. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Tech")
	FGameplayTagContainer UnlockedTechTags;

	/** Per-archetype modifiers granted by completed research (e.g., "-10% cost for Infantry"). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Tech")
	TArray<FSeinModifier> ArchetypeModifiers;

	FSeinPlayerState() = default;
	explicit FSeinPlayerState(FSeinPlayerID InPlayerID, FSeinFactionID InFactionID, uint8 InTeamID = 0);

	// --- Resource helpers ---

	/** Returns true if the player has at least the specified amount of every resource in Cost. */
	bool CanAfford(const TMap<FName, FFixedPoint>& Cost) const;

	/**
	 * Deducts resources if affordable. Returns false (and changes nothing) if
	 * any entry in Cost exceeds the player's current balance.
	 */
	bool DeductResources(const TMap<FName, FFixedPoint>& Cost);

	/** Adds resources to the player. Creates entries for new resource names. */
	void AddResources(const TMap<FName, FFixedPoint>& Amount);

	/** Returns the current amount of a named resource, or zero if not present. */
	FFixedPoint GetResource(FName ResourceName) const;

	/** Sets a named resource to an exact value. Creates the entry if needed. */
	void SetResource(FName ResourceName, FFixedPoint Amount);

	// --- Tech helpers ---

	/** Returns true if the player has all the specified tech tags (empty = always true). */
	bool HasAllTechTags(const FGameplayTagContainer& Required) const;

	/** Grant a tech tag to this player. */
	void GrantTechTag(FGameplayTag Tag);

	/** Revoke a tech tag from this player. */
	void RevokeTechTag(FGameplayTag Tag);

	/** Add an archetype-scope modifier (from completed research). */
	void AddArchetypeModifier(const FSeinModifier& Modifier);

	/** Remove all archetype modifiers originating from a specific effect/research ID. */
	void RemoveArchetypeModifiersBySource(uint32 SourceEffectID);
};

FORCEINLINE uint32 GetTypeHash(const FSeinPlayerState& State)
{
	return GetTypeHash(State.PlayerID);
}
