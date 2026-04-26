/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavigationSubsystem.cpp
 */

#include "SeinNavigationSubsystem.h"
#include "SeinNavigation.h"
#include "SeinNavigationAsset.h"
#include "Default/SeinNavigationAStar.h"
#include "Volumes/SeinNavVolume.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/Systems/SeinNavBlockerStampSystem.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinNavSubsystem, Log, All);

void USeinNavigationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Resolve the configured nav class. Fall back to the shipped A* default
	// if the setting is empty or points to a stale class.
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	UClass* NavClass = nullptr;
	if (Settings && Settings->NavigationClass.IsValid())
	{
		NavClass = Settings->NavigationClass.TryLoadClass<USeinNavigation>();
	}
	if (!NavClass || NavClass->HasAnyClassFlags(CLASS_Abstract))
	{
		NavClass = USeinNavigationAStar::StaticClass();
	}

	Navigation = NewObject<USeinNavigation>(this, NavClass, TEXT("SeinNavigation"));
	if (Navigation)
	{
		Navigation->OnNavigationInitialized(GetWorld());
	}
	else
	{
		UE_LOG(LogSeinNavSubsystem, Error, TEXT("Failed to instantiate nav class %s"),
			NavClass ? *NavClass->GetName() : TEXT("<null>"));
	}
}

void USeinNavigationSubsystem::Deinitialize()
{
	// Tear down the stamping system before nulling Navigation — the system
	// holds a weak nav ref but would still leak its own memory if dropped
	// without unregister-from-world.
	if (NavBlockerStampSystem)
	{
		if (UWorld* World = GetWorld())
		{
			if (USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>())
			{
				Sim->UnregisterSystem(NavBlockerStampSystem);
			}
		}
		delete NavBlockerStampSystem;
		NavBlockerStampSystem = nullptr;
	}

	if (Navigation)
	{
		Navigation->OnNavigationDeinitialized();
	}
	Navigation = nullptr;
	Super::Deinitialize();
}

void USeinNavigationSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	LoadBakedAssetIntoNav(InWorld);
	BindSimDelegates(InWorld);
}

void USeinNavigationSubsystem::LoadBakedAssetIntoNav(UWorld& World)
{
	if (!Navigation) return;

	for (TActorIterator<ASeinNavVolume> It(&World); It; ++It)
	{
		if (USeinNavigationAsset* Asset = It->BakedAsset)
		{
			Navigation->LoadFromAsset(Asset);
			return;
		}
	}
	// No baked asset — nav stays empty; FindPath returns no-path.
}

void USeinNavigationSubsystem::BindSimDelegates(UWorld& World)
{
	USeinWorldSubsystem* Sim = World.GetSubsystem<USeinWorldSubsystem>();
	if (!Sim || !Navigation) return;

	TWeakObjectPtr<USeinNavigation> NavWeak = Navigation;

	Sim->PathableTargetResolver.BindWeakLambda(this,
		[NavWeak](const FFixedVector& FromWorld, const FFixedVector& ToWorld, const FGameplayTagContainer& AgentTags) -> bool
		{
			USeinNavigation* Nav = NavWeak.Get();
			if (!Nav || !Nav->HasRuntimeData()) return true; // no data = permit (tests, nav-less games)
			return Nav->IsReachable(FromWorld, ToWorld, AgentTags);
		});

	// Hand the dynamic-blocker stamping over to the sim's tick loop. PreTick
	// priority 7 → after spatial-hash rebuild, before AbilityExecution where
	// MoveToAction's TickAction calls FindPath. Re-register-safe: unregister
	// any prior instance first so a level reload doesn't leak.
	if (NavBlockerStampSystem)
	{
		Sim->UnregisterSystem(NavBlockerStampSystem);
		delete NavBlockerStampSystem;
		NavBlockerStampSystem = nullptr;
	}
	NavBlockerStampSystem = new FSeinNavBlockerStampSystem(Navigation);
	Sim->RegisterSystem(NavBlockerStampSystem);
}

USeinNavigation* USeinNavigationSubsystem::GetNavigationForWorld(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	if (UWorld* World = WorldContextObject->GetWorld())
	{
		if (USeinNavigationSubsystem* Sub = World->GetSubsystem<USeinNavigationSubsystem>())
		{
			return Sub->Navigation;
		}
	}
	return nullptr;
}

bool USeinNavigationSubsystem::BeginBake(UWorld* World)
{
	if (!World) return false;
	USeinNavigationSubsystem* Sub = World->GetSubsystem<USeinNavigationSubsystem>();
	if (!Sub || !Sub->Navigation) return false;
	return Sub->Navigation->BeginBake(World);
}

bool USeinNavigationSubsystem::IsBaking(UWorld* World)
{
	if (!World) return false;
	USeinNavigationSubsystem* Sub = World->GetSubsystem<USeinNavigationSubsystem>();
	if (!Sub || !Sub->Navigation) return false;
	return Sub->Navigation->IsBaking();
}

void USeinNavigationSubsystem::RequestCancelBake(UWorld* World)
{
	if (!World) return;
	USeinNavigationSubsystem* Sub = World->GetSubsystem<USeinNavigationSubsystem>();
	if (Sub && Sub->Navigation) Sub->Navigation->RequestCancelBake();
}
