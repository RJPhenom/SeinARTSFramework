#include "Vision/SeinVisionSubsystem.h"

#include "SeinNavigationSubsystem.h"
#include "SeinNavigationGrid.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinVisionData.h"
#include "Data/SeinDiplomacyTypes.h"
#include "Events/SeinVisualEvent.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "Engine/World.h"
#include "Types/Entity.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinVision, Log, All);

namespace
{
	// Wrap MakeEntityEnteredVision / ExitedVision events by hand since the visual-event
	// factory doesn't ship dedicated makers for these yet. We reuse AbilityActivated-style
	// structure: Tag unused, PrimaryEntity = observed, SecondaryEntity = observer, PlayerID = observer's player.
	FSeinVisualEvent MakeVisibilityEvent(
		ESeinVisualEventType Type,
		FSeinEntityHandle Observed,
		FSeinPlayerID ObserverPlayer)
	{
		FSeinVisualEvent E;
		E.Type = Type;
		E.PrimaryEntity = Observed;
		E.PlayerID = ObserverPlayer;
		return E;
	}
}

void USeinVisionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	TickCount = 0;
	TickAccumulator = 0.0f;
}

void USeinVisionSubsystem::Deinitialize()
{
	VisionGroups.Reset();
	LastVisibleByPlayer.Reset();
	Super::Deinitialize();
}

void USeinVisionSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// Register the line-of-sight resolver with the sim so USeinAbility::bRequiresLineOfSight
	// can consult vision without a cross-module compile dependency.
	if (USeinWorldSubsystem* Sim = InWorld.GetSubsystem<USeinWorldSubsystem>())
	{
		Sim->LineOfSightResolver.BindWeakLambda(this,
			[this](FSeinPlayerID ObserverPlayer, const FVector& TargetWorld) -> bool
			{
				return IsLocationVisible(ObserverPlayer, TargetWorld);
			});
	}
}

TStatId USeinVisionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USeinVisionSubsystem, STATGROUP_Tickables);
}

USeinNavigationGrid* USeinVisionSubsystem::GetNavGrid() const
{
	const UWorld* World = GetWorld();
	if (!World) { return nullptr; }
	const USeinNavigationSubsystem* Nav = World->GetSubsystem<USeinNavigationSubsystem>();
	return Nav ? Nav->GetGrid() : nullptr;
}

FSeinVisionGroup* USeinVisionSubsystem::GetOrCreateGroupForPlayer(FSeinPlayerID Player)
{
	FSeinVisionGroup& Group = VisionGroups.FindOrAdd(Player);
	if (Group.Layers.Num() == 0)
	{
		// Size from nav grid.
		const USeinNavigationGrid* Grid = GetNavGrid();
		if (Grid)
		{
			const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
			const int32 Scale = 1; // MVP: vision grid matches nav grid. VisionCellsPerNavCell plugs in post-MVP.

			Group.Layers.SetNum(Grid->Layers.Num());
			for (int32 i = 0; i < Grid->Layers.Num(); ++i)
			{
				const int32 VW = Grid->Layers[i].Width / FMath::Max(1, Scale);
				const int32 VH = Grid->Layers[i].Height / FMath::Max(1, Scale);
				Group.Layers[i].Allocate(VW, VH);
			}
		}
	}
	return &Group;
}

const FSeinVisionGroup* USeinVisionSubsystem::FindGroupForPlayer(FSeinPlayerID Player) const
{
	return VisionGroups.Find(Player);
}

void USeinVisionSubsystem::Tick(float DeltaTime)
{
	// Tick rate: fixed 10 Hz for MVP. FogRenderTickRate plugin hookup lands in polish pass.
	const float Interval = 1.0f / 10.0f;

	TickAccumulator += DeltaTime;
	if (TickAccumulator < Interval) { return; }
	TickAccumulator = 0.0f;

	RunVisionTick();
}

void USeinVisionSubsystem::RunVisionTick()
{
	const UWorld* World = GetWorld();
	if (!World) { return; }

	USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>();
	USeinNavigationGrid* Grid = GetNavGrid();
	if (!Sim || !Grid || Grid->Layers.Num() == 0) { return; }

	const float CellSize = Grid->CellSize.ToFloat();
	if (CellSize <= 0.0f) { return; }

	const FVector GridOriginWorld(
		Grid->GridOrigin.X.ToFloat(),
		Grid->GridOrigin.Y.ToFloat(),
		Grid->GridOrigin.Z.ToFloat());

	// Clear all Visible bitfields at the start of the tick.
	for (TPair<FSeinPlayerID, FSeinVisionGroup>& Pair : VisionGroups)
	{
		for (FSeinVisionGroupLayer& L : Pair.Value.Layers)
		{
			L.ClearVisible();
		}
	}

	// Walk entity pool, stamp every vision emitter into its owner's group.
	TMap<FSeinPlayerID, TSet<FSeinEntityHandle>> NewVisibleByPlayer;
	Sim->GetEntityPool().ForEachEntity(
		[this, Sim, Grid, &NewVisibleByPlayer, &GridOriginWorld, CellSize]
		(FSeinEntityHandle Handle, const FSeinEntity& Entity)
		{
			const FSeinPlayerID Owner = Sim->GetEntityOwner(Handle);
			const FSeinVisionData* VisData = Sim->GetComponent<FSeinVisionData>(Handle);
			if (!VisData || !VisData->bEmits) { return; }

			FSeinVisionGroup* Group = GetOrCreateGroupForPlayer(Owner);
			if (!Group) { return; }

			// Layer the emitter sits on.
			const FFixedVector EntityLoc = Entity.Transform.GetLocation();
			const FSeinCellAddress Addr = Grid->ResolveCellAddress(EntityLoc);
			const int32 LayerIdx = Addr.IsValid() ? Addr.Layer : 0;
			if (!Group->Layers.IsValidIndex(LayerIdx)) { return; }

			const FVector EmitterWorld = EntityLoc.ToVector();
			StampCircle(
				Group->Layers[LayerIdx],
				EmitterWorld,
				VisData->SightRadius.ToFloat(),
				GridOriginWorld,
				CellSize,
				Grid->Layers[LayerIdx].Width,
				Grid->Layers[LayerIdx].Height);
		});

	// Rebuild per-player visible-entity sets. O(players * entities) — fine at RTS scale.
	Sim->GetEntityPool().ForEachEntity(
		[this, Sim, Grid, &NewVisibleByPlayer, &GridOriginWorld, CellSize]
		(FSeinEntityHandle Handle, const FSeinEntity& Entity)
		{
			const FVector EntityWorld = Entity.Transform.GetLocation().ToVector();
			for (TPair<FSeinPlayerID, FSeinVisionGroup>& Pair : VisionGroups)
			{
				if (IsLocationVisible(Pair.Key, EntityWorld))
				{
					NewVisibleByPlayer.FindOrAdd(Pair.Key).Add(Handle);
				}
			}
		});

	// Diff vs last tick → emit EntityEnteredVision / EntityExitedVision events.
	for (const TPair<FSeinPlayerID, TSet<FSeinEntityHandle>>& Pair : NewVisibleByPlayer)
	{
		const TSet<FSeinEntityHandle>& CurSet = Pair.Value;
		const TSet<FSeinEntityHandle>* PrevSet = LastVisibleByPlayer.Find(Pair.Key);
		for (const FSeinEntityHandle& H : CurSet)
		{
			if (!PrevSet || !PrevSet->Contains(H))
			{
				Sim->EnqueueVisualEvent(MakeVisibilityEvent(ESeinVisualEventType::EntityEnteredVision, H, Pair.Key));
			}
		}
		if (PrevSet)
		{
			for (const FSeinEntityHandle& H : *PrevSet)
			{
				if (!CurSet.Contains(H))
				{
					Sim->EnqueueVisualEvent(MakeVisibilityEvent(ESeinVisualEventType::EntityExitedVision, H, Pair.Key));
				}
			}
		}
	}

	LastVisibleByPlayer = MoveTemp(NewVisibleByPlayer);
	++TickCount;
}

void USeinVisionSubsystem::StampCircle(
	FSeinVisionGroupLayer& Layer,
	const FVector& EmitterWorldPos,
	float RadiusWorld,
	const FVector& GridOriginWorld,
	float CellSize,
	int32 GridWidth,
	int32 GridHeight)
{
	if (Layer.Width <= 0 || Layer.Height <= 0 || CellSize <= 0.0f) { return; }

	const int32 CellRadius = FMath::Max(1, FMath::CeilToInt(RadiusWorld / CellSize));
	const int32 CenterX = FMath::FloorToInt((EmitterWorldPos.X - GridOriginWorld.X) / CellSize);
	const int32 CenterY = FMath::FloorToInt((EmitterWorldPos.Y - GridOriginWorld.Y) / CellSize);
	const int32 MinX = FMath::Max(0, CenterX - CellRadius);
	const int32 MaxX = FMath::Min(Layer.Width - 1, CenterX + CellRadius);
	const int32 MinY = FMath::Max(0, CenterY - CellRadius);
	const int32 MaxY = FMath::Min(Layer.Height - 1, CenterY + CellRadius);

	const int32 RadiusSq = CellRadius * CellRadius;

	for (int32 Y = MinY; Y <= MaxY; ++Y)
	{
		for (int32 X = MinX; X <= MaxX; ++X)
		{
			const int32 DX = X - CenterX;
			const int32 DY = Y - CenterY;
			if (DX * DX + DY * DY > RadiusSq) { continue; }
			const int32 Idx = Layer.CellIndex(X, Y);
			if (Layer.Visible.IsValidIndex(Idx))
			{
				Layer.Visible[Idx] = 1;
				Layer.Explored[Idx] = 1;
			}
		}
	}
}

TArray<FSeinPlayerID> USeinVisionSubsystem::CollectEffectiveVisionSources(FSeinPlayerID Viewer) const
{
	TArray<FSeinPlayerID> Sources;
	Sources.Add(Viewer);
	const UWorld* World = GetWorld();
	if (!World) return Sources;
	const USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>();
	if (!Sim) return Sources;
	// Walk pairs granting SharedVision toward the viewer. Index is O(tag-count)
	// at worst; for RTS scale (≤ 8 players) it's trivial.
	const TArray<FSeinDiplomacyKey> Pairs =
		Sim->GetPairsWithDiplomacyTag(SeinARTSTags::Diplomacy_Permission_SharedVision);
	for (const FSeinDiplomacyKey& Pair : Pairs)
	{
		if (Pair.ToPlayer == Viewer && Pair.FromPlayer != Viewer)
		{
			Sources.AddUnique(Pair.FromPlayer);
		}
	}
	return Sources;
}

bool USeinVisionSubsystem::IsLocationVisible(FSeinPlayerID Player, const FVector& WorldPos) const
{
	USeinNavigationGrid* Grid = GetNavGrid();
	if (!Grid) { return false; }

	const FFixedVector WorldFixed(
		FFixedPoint::FromFloat(WorldPos.X),
		FFixedPoint::FromFloat(WorldPos.Y),
		FFixedPoint::FromFloat(WorldPos.Z));
	const FSeinCellAddress Addr = Grid->ResolveCellAddress(WorldFixed);
	if (!Addr.IsValid()) { return false; }
	if (!Grid->Layers.IsValidIndex(Addr.Layer)) { return false; }
	const int32 NavWidth = Grid->Layers[Addr.Layer].Width;
	const int32 CX = Addr.CellIndex % NavWidth;
	const int32 CY = Addr.CellIndex / NavWidth;

	// OR across own vision + any player granting SharedVision toward this viewer (DESIGN §18).
	for (const FSeinPlayerID& Src : CollectEffectiveVisionSources(Player))
	{
		const FSeinVisionGroup* Group = FindGroupForPlayer(Src);
		if (!Group || !Group->Layers.IsValidIndex(Addr.Layer)) { continue; }
		const FSeinVisionGroupLayer& L = Group->Layers[Addr.Layer];
		if (!L.IsValid(CX, CY)) { continue; }
		const int32 Idx = L.CellIndex(CX, CY);
		if (L.Visible.IsValidIndex(Idx) && L.Visible[Idx] > 0) { return true; }
	}
	return false;
}

bool USeinVisionSubsystem::IsLocationExplored(FSeinPlayerID Player, const FVector& WorldPos) const
{
	USeinNavigationGrid* Grid = GetNavGrid();
	if (!Grid) { return false; }

	const FFixedVector WorldFixed(
		FFixedPoint::FromFloat(WorldPos.X),
		FFixedPoint::FromFloat(WorldPos.Y),
		FFixedPoint::FromFloat(WorldPos.Z));
	const FSeinCellAddress Addr = Grid->ResolveCellAddress(WorldFixed);
	if (!Addr.IsValid() || !Grid->Layers.IsValidIndex(Addr.Layer)) { return false; }
	const int32 NavWidth = Grid->Layers[Addr.Layer].Width;
	const int32 CX = Addr.CellIndex % NavWidth;
	const int32 CY = Addr.CellIndex / NavWidth;

	// Explored aggregates across shared-vision sources too — once any allied
	// source has explored a cell, the viewer's fog-of-war shows it explored.
	for (const FSeinPlayerID& Src : CollectEffectiveVisionSources(Player))
	{
		const FSeinVisionGroup* Group = FindGroupForPlayer(Src);
		if (!Group || !Group->Layers.IsValidIndex(Addr.Layer)) { continue; }
		const FSeinVisionGroupLayer& L = Group->Layers[Addr.Layer];
		if (!L.IsValid(CX, CY)) { continue; }
		const int32 Idx = L.CellIndex(CX, CY);
		if (L.Explored.IsValidIndex(Idx) && L.Explored[Idx] > 0) { return true; }
	}
	return false;
}

TArray<FSeinEntityHandle> USeinVisionSubsystem::GetVisibleEntities(FSeinPlayerID Player) const
{
	TSet<FSeinEntityHandle> Combined;
	for (const FSeinPlayerID& Src : CollectEffectiveVisionSources(Player))
	{
		if (const TSet<FSeinEntityHandle>* Set = LastVisibleByPlayer.Find(Src))
		{
			Combined.Append(*Set);
		}
	}
	TArray<FSeinEntityHandle> Out;
	Out.Reserve(Combined.Num());
	for (const FSeinEntityHandle& H : Combined) { Out.Add(H); }
	Out.Sort([](const FSeinEntityHandle& A, const FSeinEntityHandle& B) { return A.Index < B.Index; });
	return Out;
}
