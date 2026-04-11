#include "Abilities/SeinLatentAction.h"

void USeinLatentAction::Complete()
{
	bCompleted = true;
}

void USeinLatentAction::Cancel()
{
	bCancelled = true;
	OnCancel();
}
