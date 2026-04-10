/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		SeinSquadComponent.cpp
 * @date:		4/3/2026
 * @author:		RJ Macklem
 * @brief:		Implementation of squad component methods.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#include "Components/SeinSquadComponent.h"

int32 FSeinSquadComponent::GetAliveCount() const
{
	return Members.Num();
}

bool FSeinSquadComponent::HasLeader() const
{
	return Leader.IsValid();
}

void FSeinSquadComponent::RemoveMember(FSeinEntityHandle Member)
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

void FSeinSquadComponent::AddMember(FSeinEntityHandle Member)
{
	if (Members.Num() < MaxSquadSize)
	{
		Members.Add(Member);
	}
}

void FSeinSquadComponent::PromoteNewLeader()
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
