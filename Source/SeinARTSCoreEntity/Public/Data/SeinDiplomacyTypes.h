/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinDiplomacyTypes.h
 * @brief   Diplomacy types (DESIGN §18). Directional per-pair tag container +
 *          hashable `FSeinDiplomacyKey` for UE's TMap.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Core/SeinPlayerID.h"
#include "SeinDiplomacyTypes.generated.h"

/**
 * Directional relation key: From A's perspective toward B. Many relations are
 * asymmetric (A unilaterally declares war; B consents to peace; A grants
 * military access independently of B) — hence the explicit pairing.
 *
 * UE TMap requires a hashable key; `TPair<FSeinPlayerID, FSeinPlayerID>` is
 * not exposed as a USTRUCT, so we wrap it here. Keyed by pair (From, To).
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinDiplomacyKey
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Diplomacy")
	FSeinPlayerID FromPlayer;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Diplomacy")
	FSeinPlayerID ToPlayer;

	FSeinDiplomacyKey() = default;
	FSeinDiplomacyKey(FSeinPlayerID InFrom, FSeinPlayerID InTo) : FromPlayer(InFrom), ToPlayer(InTo) {}

	FORCEINLINE bool operator==(const FSeinDiplomacyKey& Other) const
	{
		return FromPlayer == Other.FromPlayer && ToPlayer == Other.ToPlayer;
	}
};

FORCEINLINE uint32 GetTypeHash(const FSeinDiplomacyKey& Key)
{
	return HashCombine(GetTypeHash(Key.FromPlayer), GetTypeHash(Key.ToPlayer));
}

/**
 * Array wrapper — UE TMap value can't be `TArray<FSeinDiplomacyKey>` directly
 * in a USTRUCT UPROPERTY; wrap in an entry struct (same pattern as other
 * `{tag → array}` indices in the framework — broker capability, cell tag
 * overflow, etc.).
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinDiplomacyIndexBucket
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Diplomacy")
	TArray<FSeinDiplomacyKey> Pairs;
};

/**
 * Command payload for `SeinARTS.Command.Type.ModifyDiplomacy`. Filled by
 * `USeinDiplomacyBPFL` + read by `USeinWorldSubsystem::ProcessCommands`.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinDiplomacyCommandPayload
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Diplomacy")
	FSeinPlayerID FromPlayer;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Diplomacy")
	FSeinPlayerID ToPlayer;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Diplomacy")
	FGameplayTagContainer TagsToAdd;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Diplomacy")
	FGameplayTagContainer TagsToRemove;
};
