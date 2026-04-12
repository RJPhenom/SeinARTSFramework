/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinAbilityFactory.h
 * @date:		4/12/2026
 * @author:		RJ Macklem
 * @brief:		Factory for creating SeinAbility Blueprint classes via Content Browser.
 */

#pragma once

#include "CoreMinimal.h"
#include "Factories/BlueprintFactory.h"
#include "SeinAbilityFactory.generated.h"

/**
 * Factory that creates Blueprint classes derived from USeinAbility.
 * Appears in Content Browser under the "SeinARTS" category and the
 * default "Common" section for quick access.
 */
UCLASS()
class USeinAbilityFactory : public UBlueprintFactory
{
	GENERATED_BODY()

public:
	USeinAbilityFactory();

	// UFactory interface
	virtual bool ConfigureProperties() override;
	virtual FText GetDisplayName() const override;
	virtual uint32 GetMenuCategories() const override;
	virtual bool ShouldShowInNewMenu() const override { return true; }
};
