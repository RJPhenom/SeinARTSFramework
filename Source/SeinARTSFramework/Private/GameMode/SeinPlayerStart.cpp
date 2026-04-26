/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinPlayerStart.cpp
 * @brief   RTS player start implementation.
 */

#include "GameMode/SeinPlayerStart.h"

ASeinPlayerStart::ASeinPlayerStart(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#if WITH_EDITOR
void ASeinPlayerStart::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	// Editor-process snapshot — same pattern as ASeinActor. Conversion
	// runs once in the editor, the FFixedVector serializes to the .umap,
	// and every client (PC, ARM Mac, Surface ARM, mobile, console) reads
	// identical int64 bits at level load. Cross-arch lockstep safe.
	PlacedSimLocation = FFixedVector::FromVector(GetActorLocation());
	bSimLocationBaked = true;

	if (bFinished)
	{
		MarkPackageDirty();
	}
}
#endif
