/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSquadData.cpp
 * @brief   FSeinSquadData sim-payload implementation — membership mutation
 *          and leader promotion.
 */

#include "Components/SeinSquadData.h"

int32 FSeinSquadData::GetAliveCount() const
{
	return Members.Num();
}

bool FSeinSquadData::HasLeader() const
{
	return Leader.IsValid();
}

void FSeinSquadData::RemoveMember(FSeinEntityHandle Member)
{
	Members.RemoveAll([&Member](const FSeinEntityHandle& Handle)
	{
		return Handle == Member;
	});

	if (Leader == Member)
	{
		PromoteNewLeader();
	}
}

void FSeinSquadData::AddMember(FSeinEntityHandle Member)
{
	if (Members.Num() < MaxSquadSize)
	{
		Members.Add(Member);
	}
}

void FSeinSquadData::PromoteNewLeader()
{
	if (Members.Num() > 0)
	{
		Leader = Members[0];
	}
	else
	{
		Leader = FSeinEntityHandle::Invalid();
	}
}
