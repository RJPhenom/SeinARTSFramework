/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinActorComponent.cpp
 */

#include "Components/ActorComponents/SeinActorComponent.h"

USeinActorComponent::USeinActorComponent()
	: bOverrideFlagsResolved(0)
	, bImplementsSpawn(0)
	, bImplementsSimTick(0)
	, bImplementsDestroy(0)
	, bImplementsVisualEvent(0)
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bWantsInitializeComponent = false;
}

void USeinActorComponent::ResolveOverrideFlags()
{
	if (bOverrideFlagsResolved)
	{
		return;
	}

	const UClass* Cls = GetClass();
	// A BP override manifests as the UFUNCTION existing on a subclass past this base.
	auto IsOverridden = [this, Cls](FName EventName) -> bool
	{
		if (UFunction* Fn = Cls->FindFunctionByName(EventName))
		{
			// Skip functions defined on the base class itself — they're the
			// BlueprintImplementableEvent stubs, not overrides.
			return Fn->GetOuterUClass() != USeinActorComponent::StaticClass();
		}
		return false;
	};

	bImplementsSpawn       = IsOverridden(GET_FUNCTION_NAME_CHECKED(USeinActorComponent, ReceiveEntitySpawned))   ? 1 : 0;
	bImplementsSimTick     = IsOverridden(GET_FUNCTION_NAME_CHECKED(USeinActorComponent, ReceiveSimTick))         ? 1 : 0;
	bImplementsDestroy     = IsOverridden(GET_FUNCTION_NAME_CHECKED(USeinActorComponent, ReceiveEntityDestroyed)) ? 1 : 0;
	bImplementsVisualEvent = IsOverridden(GET_FUNCTION_NAME_CHECKED(USeinActorComponent, ReceiveVisualEvent))     ? 1 : 0;

	bOverrideFlagsResolved = 1;
}

void USeinActorComponent::DispatchSpawn(FSeinEntityHandle Handle)
{
	ResolveOverrideFlags();
	if (bImplementsSpawn)
	{
		ReceiveEntitySpawned(Handle);
	}
}

void USeinActorComponent::DispatchSimTick(FFixedPoint DeltaTime)
{
	ResolveOverrideFlags();
	if (bImplementsSimTick)
	{
		ReceiveSimTick(DeltaTime);
	}
}

void USeinActorComponent::DispatchDestroy()
{
	ResolveOverrideFlags();
	if (bImplementsDestroy)
	{
		ReceiveEntityDestroyed();
	}
}

void USeinActorComponent::DispatchVisualEvent(const FSeinVisualEvent& Event)
{
	ResolveOverrideFlags();
	if (bImplementsVisualEvent)
	{
		ReceiveVisualEvent(Event);
	}
}
