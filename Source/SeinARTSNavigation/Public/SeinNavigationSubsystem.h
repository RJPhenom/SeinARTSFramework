/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavigationSubsystem.h
 * @brief   Thin world subsystem that owns the active USeinNavigation instance.
 *
 *          Reads `USeinARTSCoreSettings::NavigationClass` on Initialize, new's
 *          up that class, and re-exposes it to the rest of the engine (move-to
 *          action, editor bake button, ability validation delegate).
 *
 *          The subsystem does NOT know what a "grid" or "navmesh" is — it only
 *          knows a USeinNavigation exists. All nav semantics live on the active
 *          subclass.
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SeinNavigationSubsystem.generated.h"

class USeinNavigation;
class USeinNavigationAsset;
class ISeinSystem;

UCLASS()
class SEINARTSNAVIGATION_API USeinNavigationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/** The active navigation instance. Never null after Initialize. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Navigation")
	USeinNavigation* GetNavigation() const { return Navigation; }

	/** Convenience accessor for BP and external callers that only have a
	 *  UObject-with-world. Returns null if the world has no nav subsystem. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Navigation", meta = (WorldContext = "WorldContextObject"))
	static USeinNavigation* GetNavigationForWorld(const UObject* WorldContextObject);

	/** Kick off a bake for every ASeinNavVolume in `World`. Returns true if
	 *  the bake started. Routes to `Nav->BeginBake(World)`. */
	static bool BeginBake(UWorld* World);

	/** Returns true if the active nav in `World` is currently baking. */
	static bool IsBaking(UWorld* World);

	/** Request bake cancellation for `World`'s active nav. */
	static void RequestCancelBake(UWorld* World);

private:

	/** The active nav for this world. Instantiated from
	 *  `USeinARTSCoreSettings::NavigationClass` during Initialize. */
	UPROPERTY(Transient)
	TObjectPtr<USeinNavigation> Navigation;

	/** Called in OnWorldBeginPlay — scans NavVolumes for a baked asset and
	 *  hands it to the nav. Idempotent. */
	void LoadBakedAssetIntoNav(UWorld& World);

	/** Binds cross-module delegates on USeinWorldSubsystem so sim code can
	 *  query nav reachability without importing nav headers. */
	void BindSimDelegates(UWorld& World);

	/** Sim-tick system that gathers FSeinExtentsData entities (those with
	 *  bBlocksNav set) each PreTick
	 *  and pushes them into Navigation->SetDynamicBlockers. Owned here so
	 *  Navigation stays a pure data/query object — the world subsystem just
	 *  ticks it. Created at OnWorldBeginPlay (when SeinWorldSubsystem is
	 *  available); torn down at Deinitialize. */
	ISeinSystem* NavBlockerStampSystem = nullptr;
};
