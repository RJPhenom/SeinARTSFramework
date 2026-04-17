/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinDynamicComponent.cpp
 */

#include "Components/ActorComponents/SeinDynamicComponent.h"
#include "StructUtils/UserDefinedStruct.h"

FInstancedStruct USeinDynamicComponent::GetSimComponent() const
{
	if (!SynthesizedSimStruct)
	{
		return FInstancedStruct();
	}

	// Build an instance of the synthesised struct, then copy field-by-field from
	// matching named UPROPERTYs on this component instance. The BP compiler
	// extension guarantees matching names and compatible property types.
	FInstancedStruct Out;
	Out.InitializeAs(SynthesizedSimStruct);

	const UClass* Cls = GetClass();
	uint8* DstBase = Out.GetMutableMemory();

	for (TFieldIterator<FProperty> It(SynthesizedSimStruct); It; ++It)
	{
		const FProperty* DstProp = *It;
		const FProperty* SrcProp = Cls->FindPropertyByName(DstProp->GetFName());
		if (!SrcProp || !SrcProp->SameType(DstProp))
		{
			continue;
		}

		const void* SrcPtr = SrcProp->ContainerPtrToValuePtr<void>(this);
		void* DstPtr = DstProp->ContainerPtrToValuePtr<void>(DstBase);
		DstProp->CopyCompleteValue(DstPtr, SrcPtr);
	}

	return Out;
}
