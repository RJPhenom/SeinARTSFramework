#include "Lib/SeinVisionBPFL.h"

#include "Vision/SeinVisionSubsystem.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Types/Entity.h"
#include "Engine/World.h"

namespace
{
	USeinVisionSubsystem* GetVision(const UObject* WorldContextObject)
	{
		if (!WorldContextObject) { return nullptr; }
		const UWorld* World = WorldContextObject->GetWorld();
		return World ? World->GetSubsystem<USeinVisionSubsystem>() : nullptr;
	}
}

bool USeinVisionBPFL::SeinIsLocationVisible(const UObject* WorldContextObject, FSeinPlayerID Player, FFixedVector WorldLocation)
{
	if (const USeinVisionSubsystem* Vis = GetVision(WorldContextObject))
	{
		return Vis->IsLocationVisible(Player, WorldLocation.ToVector());
	}
	return false;
}

bool USeinVisionBPFL::SeinIsLocationExplored(const UObject* WorldContextObject, FSeinPlayerID Player, FFixedVector WorldLocation)
{
	if (const USeinVisionSubsystem* Vis = GetVision(WorldContextObject))
	{
		return Vis->IsLocationExplored(Player, WorldLocation.ToVector());
	}
	return false;
}

bool USeinVisionBPFL::SeinIsEntityVisible(const UObject* WorldContextObject, FSeinPlayerID Player, FSeinEntityHandle Entity)
{
	if (!WorldContextObject || !Entity.IsValid()) { return false; }
	const UWorld* World = WorldContextObject->GetWorld();
	if (!World) { return false; }
	const USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>();
	const USeinVisionSubsystem* Vis = World->GetSubsystem<USeinVisionSubsystem>();
	if (!Sim || !Vis) { return false; }

	const FSeinEntity* Ent = Sim->GetEntity(Entity);
	if (!Ent) { return false; }
	return Vis->IsLocationVisible(Player, Ent->Transform.GetLocation().ToVector());
}

TArray<FSeinEntityHandle> USeinVisionBPFL::SeinGetVisibleEntities(const UObject* WorldContextObject, FSeinPlayerID Player)
{
	if (const USeinVisionSubsystem* Vis = GetVision(WorldContextObject))
	{
		return Vis->GetVisibleEntities(Player);
	}
	return {};
}

int32 USeinVisionBPFL::SeinGetVisionTickCount(const UObject* WorldContextObject)
{
	if (const USeinVisionSubsystem* Vis = GetVision(WorldContextObject))
	{
		return Vis->GetTickCount();
	}
	return 0;
}
