/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinActorFactory.h
 * @date:		3/27/2026
 * @author:		RJ Macklem
 * @brief:		Factory for creating SeinActor Blueprint classes via Content Browser.
 */

#pragma once

#include "CoreMinimal.h"
#include "Factories/BlueprintFactory.h"
#include "SeinActorFactory.generated.h"

/**
 * Factory that creates Blueprint classes derived from ASeinActor.
 * Appears in Content Browser under the "SeinARTS" category.
 * Opens a class picker dialog so designers can choose a parent class
 * (ASeinActor, or any C++/BP subclass they've created).
 */
UCLASS()
class USeinActorFactory : public UBlueprintFactory
{
	GENERATED_BODY()

public:
	USeinActorFactory();

	// UFactory interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext) override;
	virtual bool ConfigureProperties() override;
	virtual FText GetDisplayName() const override;
	virtual uint32 GetMenuCategories() const override;
	virtual bool ShouldShowInNewMenu() const override { return true; }
	virtual FName GetNewAssetThumbnailOverride() const override { return TEXT("ClassThumbnail.SeinActor"); }
	virtual FName GetNewAssetIconOverride() const override { return TEXT("ClassIcon.SeinActor"); }
};
