/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinDynamicComponent.h
 * @brief:   Base for Blueprint-authored sim components. Designers subclass this
 *           in BP, add variables in the "Sein Sim Data" category, and the
 *           SeinARTSEditor module's compile extension synthesises a UDS that
 *           mirrors those variables — letting the dynamic AC participate in
 *           the existing FInstancedStruct-based sim storage with zero sim changes.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponents/SeinActorComponent.h"
#include "SeinDynamicComponent.generated.h"

class UUserDefinedStruct;

UCLASS(Blueprintable, ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent, DisplayName = "Sein Dynamic Component"))
class SEINARTSCOREENTITY_API USeinDynamicComponent : public USeinActorComponent
{
	GENERATED_BODY()

public:
	/**
	 * Sidecar struct whose fields mirror the BP class's "Sein Sim Data" variables.
	 * Populated by USeinComponentCompilerExtension at BP compile time. Saved with
	 * the BPGC so cooked builds load it alongside the class.
	 */
	UPROPERTY(VisibleAnywhere, Category = "SeinARTS|Dynamic")
	TObjectPtr<UUserDefinedStruct> SynthesizedSimStruct;

	/**
	 * Builds an FInstancedStruct from SynthesizedSimStruct, copying each of its
	 * fields from the matching named UPROPERTY on this component instance.
	 * Returns an invalid struct when no synthesised struct exists — the entity
	 * will simply have no component of this type in sim storage.
	 */
	virtual FInstancedStruct GetSimComponent() const override;

	/** Category name on BP variables that flags them as sim-injected. */
	static constexpr const TCHAR* SimDataCategoryName = TEXT("Sein Sim Data");
};
