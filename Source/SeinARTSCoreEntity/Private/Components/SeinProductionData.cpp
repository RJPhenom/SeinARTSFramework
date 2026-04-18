/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinProductionData.cpp
 * @brief   FSeinProductionData sim-payload implementation — queue state
 *          queries and build-progress accessors.
 */

#include "Components/SeinProductionData.h"

bool FSeinProductionData::IsProducing() const
{
	return Queue.Num() > 0;
}

FFixedPoint FSeinProductionData::GetProgressPercent() const
{
	if (!IsProducing())
	{
		return FFixedPoint::Zero;
	}

	const FSeinProductionQueueEntry& Current = Queue[0];
	if (Current.TotalBuildTime <= FFixedPoint::Zero)
	{
		return FFixedPoint::One;
	}

	return CurrentBuildProgress / Current.TotalBuildTime;
}

bool FSeinProductionData::CanQueueMore() const
{
	return Queue.Num() < MaxQueueSize;
}
