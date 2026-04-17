/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinComponentCompilerExtension.cpp
 */

#include "Compile/SeinComponentCompilerExtension.h"
#include "Components/ActorComponents/SeinDynamicComponent.h"
#include "KismetCompiler.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EdGraphSchema_K2.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Types/Quat.h"
#include "Types/Rotator.h"
#include "Types/Transform.h"
#include "Core/SeinEntityHandle.h"
#include "Components/SeinComponent.h"
#include "Actor/SeinActor.h"
#include "GameplayTagContainer.h"

namespace
{
	static bool IsAllowedStructType(const UScriptStruct* S)
	{
		if (!S) return false;

		const UScriptStruct* AllowedAtoms[] = {
			TBaseStructure<FGameplayTag>::Get(),
			TBaseStructure<FGameplayTagContainer>::Get(),
			FFixedPoint::StaticStruct(),
			FFixedVector::StaticStruct(),
			FFixedQuaternion::StaticStruct(),
			FFixedRotator::StaticStruct(),
			FFixedTransform::StaticStruct(),
			FSeinEntityHandle::StaticStruct(),
		};

		for (const UScriptStruct* Allowed : AllowedAtoms)
		{
			if (S == Allowed) return true;
		}

		return S->IsChildOf(FSeinComponent::StaticStruct());
	}
}

bool USeinComponentCompilerExtension::IsDeterministicSafeType(const FEdGraphPinType& PinType)
{
	// Containers unsupported in v1: deterministic hashing of nested containers is
	// possible but out of scope.
	if (PinType.ContainerType != EPinContainerType::None)
	{
		return false;
	}

	const FName& Category = PinType.PinCategory;
	const UEdGraphSchema_K2* K2 = GetDefault<UEdGraphSchema_K2>();

	if (Category == UEdGraphSchema_K2::PC_Int)     return true;
	if (Category == UEdGraphSchema_K2::PC_Int64)   return true;
	if (Category == UEdGraphSchema_K2::PC_Boolean) return true;
	if (Category == UEdGraphSchema_K2::PC_Byte)    return true;
	if (Category == UEdGraphSchema_K2::PC_Name)    return true;

	if (Category == UEdGraphSchema_K2::PC_Struct)
	{
		const UScriptStruct* S = Cast<UScriptStruct>(PinType.PinSubCategoryObject.Get());
		return IsAllowedStructType(S);
	}

	if (Category == UEdGraphSchema_K2::PC_Class || Category == UEdGraphSchema_K2::PC_SoftClass)
	{
		if (const UClass* ClassRef = Cast<UClass>(PinType.PinSubCategoryObject.Get()))
		{
			return ClassRef->IsChildOf(AActor::StaticClass()) || ClassRef == UObject::StaticClass();
		}
		return true;
	}

	// Disallow: float, double, real, string, text, object refs, interfaces, delegates.
	return false;
}

FString USeinComponentCompilerExtension::DescribePinType(const FEdGraphPinType& PinType)
{
	FString Base = PinType.PinCategory.ToString();
	if (const UObject* SubObj = PinType.PinSubCategoryObject.Get())
	{
		Base += TEXT("<") + SubObj->GetName() + TEXT(">");
	}
	if (PinType.ContainerType == EPinContainerType::Array) Base = FString::Printf(TEXT("Array<%s>"), *Base);
	else if (PinType.ContainerType == EPinContainerType::Set) Base = FString::Printf(TEXT("Set<%s>"), *Base);
	else if (PinType.ContainerType == EPinContainerType::Map) Base = FString::Printf(TEXT("Map<%s>"), *Base);
	return Base;
}

void USeinComponentCompilerExtension::ProcessBlueprintCompiled(const FKismetCompilerContext& CompilationContext, const FBlueprintCompiledData& Data)
{
	UBlueprint* BP = CompilationContext.Blueprint;
	UBlueprintGeneratedClass* BPGC = CompilationContext.NewClass;
	if (!BP || !BPGC)
	{
		return;
	}

	// Only run for BPs whose generated class derives from USeinDynamicComponent.
	if (!BPGC->IsChildOf(USeinDynamicComponent::StaticClass()))
	{
		return;
	}

	FCompilerResultsLog& Log = CompilationContext.MessageLog;

	// Collect BP variables flagged as sim data.
	const FString SimCat(USeinDynamicComponent::SimDataCategoryName);
	TArray<const FBPVariableDescription*> SimVars;
	for (const FBPVariableDescription& Var : BP->NewVariables)
	{
		if (Var.Category.ToString().Equals(SimCat, ESearchCase::IgnoreCase))
		{
			SimVars.Add(&Var);
		}
	}

	// Validate types; accumulate all errors so designers see every offender at once.
	bool bAllValid = true;
	for (const FBPVariableDescription* Var : SimVars)
	{
		if (!IsDeterministicSafeType(Var->VarType))
		{
			Log.Error(*FString::Printf(
				TEXT("Sein Sim Data variable '%s' has non-deterministic type '%s'. Allowed: int32/int64, bool, byte/enum, FName, FGameplayTag(Container), FFixedPoint/Vector/Quat/Transform, FSeinEntityHandle, any FSeinComponent-derived struct, or class ref to AActor."),
				*Var->VarName.ToString(),
				*DescribePinType(Var->VarType)));
			bAllValid = false;
		}
	}

	if (!bAllValid)
	{
		return;
	}

	// Empty sim data: clear the synthesised struct on the CDO and stop.
	if (SimVars.Num() == 0)
	{
		const_cast<FKismetCompilerContext&>(CompilationContext).AddPostCDOCompiledStep(
			[](const UObject::FPostCDOCompiledContext& Ctx, UObject* DefaultObject)
			{
				if (USeinDynamicComponent* Target = Cast<USeinDynamicComponent>(DefaultObject))
				{
					Target->SynthesizedSimStruct = nullptr;
				}
			});
		return;
	}

	// Find or create the sidecar UDS as a subobject of the BPGC so it saves with
	// the asset and cooked builds load it alongside the class.
	const FName StructName(*FString::Printf(TEXT("%s_SimData"), *BPGC->GetName()));
	UUserDefinedStruct* SimStruct = FindObject<UUserDefinedStruct>(BPGC, *StructName.ToString());

	if (!SimStruct)
	{
		SimStruct = NewObject<UUserDefinedStruct>(BPGC, StructName, RF_Public | RF_Transactional);
		SimStruct->EditorData = NewObject<UUserDefinedStructEditorData>(SimStruct, NAME_None, RF_Transactional);
		SimStruct->Guid = FGuid::NewGuid();
		SimStruct->SetMetaData(TEXT("BlueprintType"), TEXT("true"));
	}

	// Strip existing fields (via copy-then-remove to avoid iterator invalidation)
	// then re-add from scratch to match SimVars.
	{
		TArray<FGuid> ToRemove;
		for (const FStructVariableDescription& D : FStructureEditorUtils::GetVarDesc(SimStruct))
		{
			ToRemove.Add(D.VarGuid);
		}
		for (const FGuid& G : ToRemove)
		{
			FStructureEditorUtils::RemoveVariable(SimStruct, G);
		}
	}

	for (const FBPVariableDescription* Var : SimVars)
	{
		FStructureEditorUtils::AddVariable(SimStruct, Var->VarType);

		// AddVariable appends with an auto-generated display name; rename so the
		// UDS field matches the BP variable by name (runtime reflection uses FName).
		TArray<FStructVariableDescription>& VarDescs = FStructureEditorUtils::GetVarDesc(SimStruct);
		if (VarDescs.Num() > 0)
		{
			const FGuid NewGuid = VarDescs.Last().VarGuid;
			FStructureEditorUtils::RenameVariable(SimStruct, NewGuid, Var->VarName.ToString());
		}
	}

	FStructureEditorUtils::CompileStructure(SimStruct);

	// Assign on the CDO once the compile pipeline has finished baking it.
	UUserDefinedStruct* TargetStruct = SimStruct;
	const_cast<FKismetCompilerContext&>(CompilationContext).AddPostCDOCompiledStep(
		[TargetStruct](const UObject::FPostCDOCompiledContext& Ctx, UObject* DefaultObject)
		{
			if (USeinDynamicComponent* Target = Cast<USeinDynamicComponent>(DefaultObject))
			{
				Target->SynthesizedSimStruct = TargetStruct;
			}
		});
}
