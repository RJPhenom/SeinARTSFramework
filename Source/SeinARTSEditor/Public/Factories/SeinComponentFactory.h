/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinComponentFactory.h
 * @brief:   Factory for creating SeinARTS Component Blueprint assets — BP subclasses
 *           of USeinActorComponent (or one of the typed wrappers) that designers
 *           attach to SeinUnit BPs via the standard "+ Add Component" menu.
 */

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "SeinComponentFactory.generated.h"

UCLASS()
class USeinComponentFactory : public UFactory
{
	GENERATED_BODY()

public:
	USeinComponentFactory();

	// UFactory interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext) override;
	virtual bool ConfigureProperties() override;
	virtual FText GetDisplayName() const override;
	virtual uint32 GetMenuCategories() const override;
	virtual bool ShouldShowInNewMenu() const override { return true; }
	virtual FName GetNewAssetThumbnailOverride() const override { return TEXT("ClassThumbnail.SeinComponent"); }
	virtual FName GetNewAssetIconOverride() const override { return TEXT("ClassIcon.SeinComponent"); }

private:
	UPROPERTY()
	TSubclassOf<class UObject> ParentClass;

	EBlueprintType BlueprintType;
};
