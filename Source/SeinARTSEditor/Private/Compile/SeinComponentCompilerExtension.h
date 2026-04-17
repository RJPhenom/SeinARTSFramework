/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinComponentCompilerExtension.h
 * @brief:   BP compiler extension that synthesises a UUserDefinedStruct mirroring
 *           the BP class's "Sein Sim Data" variables — this lets Blueprint
 *           subclasses of USeinDynamicComponent participate in the existing
 *           FInstancedStruct-based sim storage with zero sim-side changes.
 *
 *           Also emits compile errors when sim-flagged variables use
 *           non-deterministic types (float, FVector, etc.).
 */

#pragma once

#include "CoreMinimal.h"
#include "BlueprintCompilerExtension.h"
#include "SeinComponentCompilerExtension.generated.h"

UCLASS()
class USeinComponentCompilerExtension : public UBlueprintCompilerExtension
{
	GENERATED_BODY()

protected:
	virtual void ProcessBlueprintCompiled(const FKismetCompilerContext& CompilationContext, const FBlueprintCompiledData& Data) override;

private:
	/** True if this BP variable's pin type maps to a deterministic-safe sim field. */
	static bool IsDeterministicSafeType(const FEdGraphPinType& PinType);

	/** Human-readable description of a pin type (for compiler error messages). */
	static FString DescribePinType(const FEdGraphPinType& PinType);
};
