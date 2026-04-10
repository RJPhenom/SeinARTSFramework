/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		SeinLatentAction.cpp
 * @date:		4/3/2026
 * @author:		RJ Macklem
 * @brief:		Base latent action implementation.
 * @disclaimer: This code was generated in part by an AI language model.
 */

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
