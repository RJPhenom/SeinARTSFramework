/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinModifier.h
 * @brief   Attribute modifier for the deterministic attribute system.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Core/SeinEntityHandle.h"
#include "GameplayTagContainer.h"
#include "SeinModifier.generated.h"

/** How the modifier value is applied to the base attribute. */
UENUM(BlueprintType)
enum class ESeinModifierOp : uint8
{
	Add,        // Final += Value
	Multiply,   // Final *= Value
	Override    // Final = Value (last override wins)
};

/** Whether the modifier targets a single entity or all entities of an archetype. */
UENUM(BlueprintType)
enum class ESeinModifierScope : uint8
{
	Instance,   // Affects one entity
	Archetype   // Affects all entities of an archetype owned by a player
};

/**
 * A single attribute modifier. Targets a specific FFixedPoint field on a
 * component struct identified by UScriptStruct* and FName.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinModifier
{
	GENERATED_BODY()

	// --- Target ---

	/** The component struct type that contains the field to modify. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Ability|Target")
	TObjectPtr<UScriptStruct> TargetComponentType;

	/** The FFixedPoint field name within the target component. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Ability|Target")
	FName TargetFieldName;

	// --- Modifier ---

	/** How the value is applied (Add, Multiply, Override). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Modifier")
	ESeinModifierOp Operation = ESeinModifierOp::Add;

	/** The modifier value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Modifier")
	FFixedPoint Value;

	// --- Scope ---

	/** Whether this targets a single entity or an archetype. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Modifier")
	ESeinModifierScope Scope = ESeinModifierScope::Instance;

	// --- Source Tracking ---

	/** Entity that applied this modifier. */
	UPROPERTY()
	FSeinEntityHandle SourceEntity;

	/** Effect instance ID that owns this modifier (0 = standalone). */
	UPROPERTY()
	uint32 SourceEffectID = 0;

	// --- Archetype Targeting ---

	/** For archetype-scope modifiers: gameplay tag identifying the target archetype. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Ability|Target", meta = (EditCondition = "Scope == ESeinModifierScope::Archetype"))
	FGameplayTag TargetArchetypeTag;

	FSeinModifier() = default;
};

FORCEINLINE uint32 GetTypeHash(const FSeinModifier& Mod)
{
	uint32 Hash = GetTypeHash(Mod.TargetFieldName);
	Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Mod.Operation)));
	Hash = HashCombine(Hash, GetTypeHash(Mod.Value));
	Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Mod.Scope)));
	Hash = HashCombine(Hash, GetTypeHash(Mod.SourceEntity));
	Hash = HashCombine(Hash, GetTypeHash(Mod.SourceEffectID));
	return Hash;
}
