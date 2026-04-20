/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSimComponentFactory.h
 * @brief   Right-click → Component factory. Creates a UUserDefinedStruct
 *          tagged with the `SeinDeterministic` package metadata key so
 *          the framework's pin-type / struct-viewer filters know to apply
 *          the deterministic-types whitelist when designers edit it.
 *
 *          The synthesized non-Blueprintable wrapper AC for actor BP
 *          composition is created by the AC synthesis pipeline (separate
 *          subsystem) when this UDS is saved. See DESIGN.md §2 Components.
 */

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "SeinSimComponentFactory.generated.h"

class UStruct;
class UUserDefinedStruct;

UCLASS()
class USeinSimComponentFactory : public UFactory
{
	GENERATED_BODY()

public:
	USeinSimComponentFactory();

	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual FText GetDisplayName() const override;
	virtual uint32 GetMenuCategories() const override;
	virtual bool ShouldShowInNewMenu() const override { return true; }
	virtual FName GetNewAssetThumbnailOverride() const override { return TEXT("ClassThumbnail.SeinSimComponent"); }
	virtual FName GetNewAssetIconOverride() const override { return TEXT("ClassIcon.SeinSimComponent"); }

	/** Struct-level UField metadata key written on UDSes created by this factory,
	 *  and present on every native USTRUCT marked `USTRUCT(meta = (SeinDeterministic))`.
	 *  Pin-type + struct-viewer filters key off this via UStruct::HasMetaData. */
	static const FName SeinDeterministicMetaKey;

	/** Returns true iff the struct carries the `SeinDeterministic` UField metadata.
	 *  Works for both native USTRUCTs (meta populated by UHT from the USTRUCT macro)
	 *  and UDSes (meta populated by this factory on creation). */
	static bool IsSeinDeterministicStruct(const UStruct* Struct);
};
