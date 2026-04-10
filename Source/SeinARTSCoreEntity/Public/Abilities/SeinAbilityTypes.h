#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "SeinAbilityTypes.generated.h"

/**
 * Determines what kind of target an ability requires.
 */
UENUM(BlueprintType)
enum class ESeinAbilityTargetType : uint8
{
	None,		// No target needed
	Self,		// Auto-targets self
	Entity,		// Targets another entity
	Point,		// Targets a world location
	Area,		// Location + radius
	Passive		// Always active
};

/**
 * Tag-based gating requirements for ability activation.
 * The owning entity must satisfy both tag sets for the ability to activate.
 */
USTRUCT(BlueprintType)
struct SEINARTSCOREENTITY_API FSeinAbilityRequirements
{
	GENERATED_BODY()

	/** Entity must have ALL of these tags to activate */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ability")
	FGameplayTagContainer RequiredTags;

	/** Entity must have NONE of these tags to activate */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ability")
	FGameplayTagContainer BlockedTags;
};
