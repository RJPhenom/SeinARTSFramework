/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinVisionSubsystem.h
 * @brief   World subsystem owning per-player VisionGroups + the vision tick
 *          (DESIGN.md §12). Ticks at FogRenderTickRate. Walks entities with
 *          FSeinVisionData, stamps visibility circles into their owner's
 *          VisionGroup, and emits EntityEnteredVision/EntityExitedVision
 *          events.
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "Core/SeinPlayerID.h"
#include "Core/SeinEntityHandle.h"
#include "Vision/SeinVisionGroup.h"
#include "SeinVisionSubsystem.generated.h"

class USeinNavigationGrid;

UCLASS()
class SEINARTSNAVIGATION_API USeinVisionSubsystem : public UWorldSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	// UWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return true; }
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override { return false; }

	/** Lookup the VisionGroup for a player ID (creates on first query). */
	FSeinVisionGroup* GetOrCreateGroupForPlayer(FSeinPlayerID Player);

	/** Get-only lookup. Returns nullptr if no group exists yet. */
	const FSeinVisionGroup* FindGroupForPlayer(FSeinPlayerID Player) const;

	/** True if the cell at `WorldPos` is currently visible in `Player`'s VisionGroup. */
	bool IsLocationVisible(FSeinPlayerID Player, const FVector& WorldPos) const;

	/** True if the cell at `WorldPos` has been explored by `Player` at any past tick. */
	bool IsLocationExplored(FSeinPlayerID Player, const FVector& WorldPos) const;

	/** Entities currently visible for a given player (walks the entity pool each call). */
	TArray<FSeinEntityHandle> GetVisibleEntities(FSeinPlayerID Player) const;

	/** Number of vision ticks completed since subsystem init. */
	int32 GetTickCount() const { return TickCount; }

private:
	/** Resolve nav grid pointer (cached per tick). */
	USeinNavigationGrid* GetNavGrid() const;

	/** Run a single vision tick — stamp all emitters into their groups; emit enter/exit events. */
	void RunVisionTick();

	/** Stamp a radius circle for one emitter into a group layer. */
	void StampCircle(
		FSeinVisionGroupLayer& Layer,
		const FVector& EmitterWorldPos,
		float RadiusWorld,
		const FVector& GridOriginWorld,
		float CellSize,
		int32 GridWidth,
		int32 GridHeight);

	/** Per-player last-tick visible entity sets for enter/exit event diffing. */
	TMap<FSeinPlayerID, TSet<FSeinEntityHandle>> LastVisibleByPlayer;

	UPROPERTY()
	TMap<FSeinPlayerID, FSeinVisionGroup> VisionGroups;

	float TickAccumulator = 0.0f;
	int32 TickCount = 0;
};
