/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavModifierComponent.h
 * @brief   Per-primitive bake override (DESIGN.md §13 "Nav bake pipeline").
 *          Parallels UE's UNavModifierComponent: attach to a primitive whose
 *          default nav-blocking behavior needs overriding (force-blocked /
 *          force-walkable / tag-stamp).
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "SeinNavModifierComponent.generated.h"

UENUM(BlueprintType)
enum class ESeinNavModifierMode : uint8
{
	None         UMETA(DisplayName = "None (respect collision)"),
	ForceBlocked UMETA(DisplayName = "Force Blocked"),
	ForceWalkable UMETA(DisplayName = "Force Walkable"),
	StampTags    UMETA(DisplayName = "Stamp Tags"),
};

UCLASS(ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent, DisplayName = "Sein Nav Modifier"))
class SEINARTSNAVIGATION_API USeinNavModifierComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USeinNavModifierComponent();

	/** How this primitive should be treated during the nav bake. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Navigation")
	ESeinNavModifierMode Mode = ESeinNavModifierMode::None;

	/**
	 * When Mode == StampTags, these tags are written into the covered cells'
	 * tag palette slots at bake. Combined with any tags already present.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Navigation",
		meta = (EditCondition = "Mode == ESeinNavModifierMode::StampTags"))
	FGameplayTagContainer StampTags;

	/**
	 * When Mode == ForceWalkable, this movement-cost multiplier is written
	 * into affected cells (1 = default cost; 255 = near-impassable).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Navigation",
		meta = (EditCondition = "Mode == ESeinNavModifierMode::ForceWalkable",
		        ClampMin = "1", ClampMax = "255"))
	uint8 ForcedMovementCost = 1;
};
