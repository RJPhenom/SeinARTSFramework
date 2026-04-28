/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNetRelay.cpp
 */

#include "SeinNetRelay.h"
#include "SeinARTSNet.h"
#include "SeinNetSubsystem.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"

ASeinNetRelay::ASeinNetRelay()
{
	// Replicated so its RPCs route through the NetDriver. NetLoadOnClient = false
	// because we always spawn dynamically per-connection, never as a level actor.
	bReplicates = true;
	bNetLoadOnClient = false;

	// Replicate only to the owning client (the PC this relay is for).
	// Per-PC relays are not relevant to other clients — fan-out happens by
	// the server iterating ALL relays and calling each one's ClientRPC, which
	// reaches that relay's owning client. No always-relevant flag needed.
	bOnlyRelevantToOwner = true;

	// No tick — RPC-driven only.
	PrimaryActorTick.bCanEverTick = false;
	SetActorTickEnabled(false);
}

void ASeinNetRelay::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASeinNetRelay, AssignedPlayerID);
	DOREPLIFETIME(ASeinNetRelay, SessionSeed);
}

void ASeinNetRelay::OnRep_AssignedPlayerID()
{
	UE_LOG(LogSeinNet, Log,
		TEXT("[Client] OnRep_AssignedPlayerID: slot=%u  seed=%lld  Owner=%s"),
		AssignedPlayerID.Value, SessionSeed, *GetNameSafe(GetOwner()));

	// Latch into the subsystem so SubmitLocalCommand and gameplay code can read
	// it. Note: this OnRep fires only on the owning client (relay is owner-only).
	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->NotifyLocalSlotAssigned(this, AssignedPlayerID, SessionSeed);
	}
}

void ASeinNetRelay::BeginPlay()
{
	Super::BeginPlay();

	const ENetMode NetMode = GetNetMode();
	const ENetRole LocalRole = GetLocalRole();
	UE_LOG(LogSeinNet, Verbose,
		TEXT("ASeinNetRelay::BeginPlay  NetMode=%d  LocalRole=%d  Owner=%s"),
		(int32)NetMode, (int32)LocalRole, *GetNameSafe(GetOwner()));

	// Register with the GI subsystem on whichever process this body is running in.
	// On the server, the relay was spawned by the subsystem so it's already tracked
	// — but we re-register to handle seamless travel / late binding paths.
	// On the client, this is the relay's first appearance and we need the
	// subsystem to learn its local relay handle.
	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->RegisterRelay(this);
	}
}

void ASeinNetRelay::EndPlay(const EEndPlayReason::Type Reason)
{
	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->UnregisterRelay(this);
	}
	Super::EndPlay(Reason);
}

USeinNetSubsystem* ASeinNetRelay::GetNetSubsystem() const
{
	const UWorld* World = GetWorld();
	if (!World) return nullptr;
	UGameInstance* GI = World->GetGameInstance();
	if (!GI) return nullptr;
	return GI->GetSubsystem<USeinNetSubsystem>();
}

void ASeinNetRelay::Server_SubmitCommands_Implementation(int32 TurnId, const TArray<FSeinCommand>& Commands)
{
	UE_LOG(LogSeinNet, Verbose,
		TEXT("[Server] Recv submission  TurnId=%d  Count=%d  FromSlot=%u  Owner=%s"),
		TurnId, Commands.Num(), AssignedPlayerID.Value, *GetNameSafe(GetOwner()));

	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->ServerHandleSubmission(this, TurnId, Commands);
	}
}

void ASeinNetRelay::Client_ReceiveTurn_Implementation(int32 TurnId, const TArray<FSeinCommand>& Commands)
{
	UE_LOG(LogSeinNet, Verbose,
		TEXT("[Client] Recv turn  TurnId=%d  Count=%d  Owner=%s"),
		TurnId, Commands.Num(), *GetNameSafe(GetOwner()));

	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->ClientHandleTurn(TurnId, Commands);
	}
}

void ASeinNetRelay::Client_StartSession_Implementation()
{
	UE_LOG(LogSeinNet, Log, TEXT("[Client] Client_StartSession received  Owner=%s"), *GetNameSafe(GetOwner()));
	if (USeinNetSubsystem* Net = GetNetSubsystem())
	{
		Net->StartLocalSession();
	}
}
