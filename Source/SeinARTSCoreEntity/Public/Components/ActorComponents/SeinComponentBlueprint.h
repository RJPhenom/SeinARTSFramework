/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinComponentBlueprint.h
 * @brief:   Blueprint asset subclass for Sein UActorComponent types.
 *           Gives the editor a unique SupportedClass for asset-type actions,
 *           colors, and filtering (mirrors USeinAbilityBlueprint / USeinActorBlueprint).
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/Blueprint.h"
#include "SeinComponentBlueprint.generated.h"

UCLASS(BlueprintType)
class SEINARTSCOREENTITY_API USeinComponentBlueprint : public UBlueprint
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual bool SupportedByDefaultBlueprintFactory() const override { return false; }
#endif
};
