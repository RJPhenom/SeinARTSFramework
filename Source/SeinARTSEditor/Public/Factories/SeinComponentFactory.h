/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinComponentFactory.h
 * @date:		4/12/2026
 * @author:		RJ Macklem
 * @brief:		Factory for creating SeinARTS component structs via Content Browser.
 */

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "SeinComponentFactory.generated.h"

/**
 * Factory that creates UserDefinedStruct assets for use as ECS components.
 * Appears in Content Browser under the "SeinARTS" category.
 *
 * Component structs are the data containers attached to sim entities.
 * Designers can create new component types from the editor and populate
 * them with FFixedPoint, FFixedVector, and other deterministic fields.
 */
UCLASS()
class USeinComponentFactory : public UFactory
{
	GENERATED_BODY()

public:
	USeinComponentFactory();

	// UFactory interface
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual FText GetDisplayName() const override;
	virtual uint32 GetMenuCategories() const override;
	virtual bool ShouldShowInNewMenu() const override { return true; }
};
