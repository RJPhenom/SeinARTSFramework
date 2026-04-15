#include "Movement/SeinMovementProfile.h"
#include "SeinPathfinder.h"
#include "SeinPathTypes.h"

void USeinMovementProfile::BuildPath(USeinPathfinder* Pathfinder, const FSeinPathRequest& Request, FSeinPath& OutPath) const
{
	if (!Pathfinder)
	{
		OutPath.Clear();
		return;
	}
	OutPath = Pathfinder->FindPath(Request);
}
