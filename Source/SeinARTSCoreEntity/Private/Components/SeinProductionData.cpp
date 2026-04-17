/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		SeinProductionComponent.cpp
 * @date:		4/3/2026
 * @author:		RJ Macklem
 * @brief:		Implementation of production component methods.
 * @disclaimer: This code was generated in part by an AI language model.
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
