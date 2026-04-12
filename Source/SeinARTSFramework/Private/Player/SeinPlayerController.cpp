/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSPlayerController.cpp
 * @brief   RTS player controller implementation.
 */

#include "Player/SeinARTSPlayerController.h"
#include "Player/SeinCameraPawn.h"
#include "Player/SeinSelectionComponent.h"
#include "Input/SeinInputConfig.h"
#include "Actor/SeinActor.h"
#include "Actor/SeinActorComponent.h"
#include "Data/SeinArchetypeDefinition.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Input/SeinCommand.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "DrawDebugHelpers.h"

ASeinARTSPlayerController::ASeinARTSPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
	DefaultMouseCursor = EMouseCursor::Default;

	// Load default input config from plugin content if not already set
	static ConstructorHelpers::FObjectFinder<USeinInputConfig> DefaultConfig(
		TEXT("/SeinARTSFramework/Input/DA_SeinDefaultInputConfig"));
	if (DefaultConfig.Succeeded())
	{
		InputConfig = DefaultConfig.Object;
	}
}

void ASeinARTSPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// Add input mapping context
	if (InputConfig && InputConfig->DefaultMappingContext)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(InputConfig->DefaultMappingContext, InputConfig->DefaultMappingPriority);
		}
	}
}

void ASeinARTSPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateHover();
	UpdateCommandDrag();
	PurgeStaleSelection();
	LogCameraUpdate();
}

// ==================== Input Setup ====================

void ASeinARTSPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (!InputConfig)
	{
		UE_LOG(LogTemp, Warning, TEXT("SeinARTSPlayerController: No InputConfig assigned. Input will not function."));
		return;
	}

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EIC)
	{
		UE_LOG(LogTemp, Error, TEXT("SeinARTSPlayerController: InputComponent is not UEnhancedInputComponent. Check project input settings."));
		return;
	}

	// Selection
	if (InputConfig->IA_Select)
	{
		EIC->BindAction(InputConfig->IA_Select, ETriggerEvent::Started, this, &ASeinARTSPlayerController::OnSelectPressed);
		EIC->BindAction(InputConfig->IA_Select, ETriggerEvent::Completed, this, &ASeinARTSPlayerController::OnSelectReleased);
	}

	// Command (fire on release for drag order support)
	if (InputConfig->IA_Command)
	{
		EIC->BindAction(InputConfig->IA_Command, ETriggerEvent::Started, this, &ASeinARTSPlayerController::OnCommandStarted);
		EIC->BindAction(InputConfig->IA_Command, ETriggerEvent::Completed, this, &ASeinARTSPlayerController::OnCommandReleased);
	}

	// Camera — keyboard pan (WASD / arrows)
	if (InputConfig->IA_KeyPan)
	{
		EIC->BindAction(InputConfig->IA_KeyPan, ETriggerEvent::Triggered, this, &ASeinARTSPlayerController::OnCameraPan);
	}
	// Camera — keyboard rotate (Q/E)
	if (InputConfig->IA_KeyRotate)
	{
		EIC->BindAction(InputConfig->IA_KeyRotate, ETriggerEvent::Triggered, this, &ASeinARTSPlayerController::OnCameraRotate);
	}
	// Camera — mouse zoom (scroll wheel)
	if (InputConfig->IA_MouseZoom)
	{
		EIC->BindAction(InputConfig->IA_MouseZoom, ETriggerEvent::Triggered, this, &ASeinARTSPlayerController::OnCameraZoom);
	}
	// Camera — keyboard zoom (Z/X)
	if (InputConfig->IA_KeyZoom)
	{
		EIC->BindAction(InputConfig->IA_KeyZoom, ETriggerEvent::Triggered, this, &ASeinARTSPlayerController::OnCameraZoomKeyboard);
	}
	// Camera — follow (F)
	if (InputConfig->IA_FollowCamera)
	{
		EIC->BindAction(InputConfig->IA_FollowCamera, ETriggerEvent::Started, this, &ASeinARTSPlayerController::OnCameraFollowPressed);
	}
	// Camera — reset rotation (Backspace)
	if (InputConfig->IA_ResetCamera)
	{
		EIC->BindAction(InputConfig->IA_ResetCamera, ETriggerEvent::Started, this, &ASeinARTSPlayerController::OnCameraResetPressed);
	}
	// Camera — MMB pan (chorded in mapping context)
	if (InputConfig->IA_MousePan)
	{
		EIC->BindAction(InputConfig->IA_MousePan, ETriggerEvent::Triggered, this, &ASeinARTSPlayerController::OnCameraMMBPan);
	}
	// Camera — Alt+MMB rotate (chorded in mapping context)
	if (InputConfig->IA_MouseRotate)
	{
		EIC->BindAction(InputConfig->IA_MouseRotate, ETriggerEvent::Triggered, this, &ASeinARTSPlayerController::OnCameraAltRotate);
	}

	// Ping (Ctrl+MMB)
	if (InputConfig->IA_Ping)
	{
		EIC->BindAction(InputConfig->IA_Ping, ETriggerEvent::Started, this, &ASeinARTSPlayerController::OnPingPressed);
	}

	// Focus cycling
	if (InputConfig->IA_CycleFocus)
	{
		EIC->BindAction(InputConfig->IA_CycleFocus, ETriggerEvent::Started, this, &ASeinARTSPlayerController::OnCycleFocusPressed);
	}

	// Modifiers
	if (InputConfig->IA_ModifierShift)
	{
		EIC->BindAction(InputConfig->IA_ModifierShift, ETriggerEvent::Started, this, &ASeinARTSPlayerController::OnModifierShift);
		EIC->BindAction(InputConfig->IA_ModifierShift, ETriggerEvent::Completed, this, &ASeinARTSPlayerController::OnModifierShift);
	}
	if (InputConfig->IA_ModifierCtrl)
	{
		EIC->BindAction(InputConfig->IA_ModifierCtrl, ETriggerEvent::Started, this, &ASeinARTSPlayerController::OnModifierCtrl);
		EIC->BindAction(InputConfig->IA_ModifierCtrl, ETriggerEvent::Completed, this, &ASeinARTSPlayerController::OnModifierCtrl);
	}
	if (InputConfig->IA_ModifierAlt)
	{
		EIC->BindAction(InputConfig->IA_ModifierAlt, ETriggerEvent::Started, this, &ASeinARTSPlayerController::OnModifierAlt);
		EIC->BindAction(InputConfig->IA_ModifierAlt, ETriggerEvent::Completed, this, &ASeinARTSPlayerController::OnModifierAlt);
	}

	// Control groups (0-9) — bind to individual handler methods since BindAction requires member function pointers
	using HandlerFn = void (ASeinARTSPlayerController::*)(const FInputActionValue&);
	static const HandlerFn ControlGroupHandlers[10] = {
		&ASeinARTSPlayerController::OnControlGroup0, &ASeinARTSPlayerController::OnControlGroup1,
		&ASeinARTSPlayerController::OnControlGroup2, &ASeinARTSPlayerController::OnControlGroup3,
		&ASeinARTSPlayerController::OnControlGroup4, &ASeinARTSPlayerController::OnControlGroup5,
		&ASeinARTSPlayerController::OnControlGroup6, &ASeinARTSPlayerController::OnControlGroup7,
		&ASeinARTSPlayerController::OnControlGroup8, &ASeinARTSPlayerController::OnControlGroup9,
	};

	for (int32 i = 0; i < InputConfig->IA_ControlGroups.Num() && i < 10; ++i)
	{
		if (InputConfig->IA_ControlGroups[i])
		{
			EIC->BindAction(InputConfig->IA_ControlGroups[i], ETriggerEvent::Started, this, ControlGroupHandlers[i]);
		}
	}

	// Action slot hotkeys (12 slots for ability/action panel)
	using ActionSlotFn = void (ASeinARTSPlayerController::*)(const FInputActionValue&);
	static const ActionSlotFn ActionSlotHandlers[12] = {
		&ASeinARTSPlayerController::OnActionSlot0,  &ASeinARTSPlayerController::OnActionSlot1,
		&ASeinARTSPlayerController::OnActionSlot2,  &ASeinARTSPlayerController::OnActionSlot3,
		&ASeinARTSPlayerController::OnActionSlot4,  &ASeinARTSPlayerController::OnActionSlot5,
		&ASeinARTSPlayerController::OnActionSlot6,  &ASeinARTSPlayerController::OnActionSlot7,
		&ASeinARTSPlayerController::OnActionSlot8,  &ASeinARTSPlayerController::OnActionSlot9,
		&ASeinARTSPlayerController::OnActionSlot10, &ASeinARTSPlayerController::OnActionSlot11,
	};

	for (int32 i = 0; i < InputConfig->IA_ActionSlots.Num() && i < 12; ++i)
	{
		if (InputConfig->IA_ActionSlots[i])
		{
			EIC->BindAction(InputConfig->IA_ActionSlots[i], ETriggerEvent::Started, this, ActionSlotHandlers[i]);
		}
	}

	// Menu / Escape
	if (InputConfig->IA_Menu)
	{
		EIC->BindAction(InputConfig->IA_Menu, ETriggerEvent::Started, this, &ASeinARTSPlayerController::OnMenuKeyPressed);
	}
}

// ==================== Input Handlers ====================

void ASeinARTSPlayerController::OnSelectPressed(const FInputActionValue& Value)
{
	bSelectHeld = true;

	// Record start position for potential marquee
	float MouseX, MouseY;
	if (GetMousePosition(MouseX, MouseY))
	{
		MarqueeStart = FVector2D(MouseX, MouseY);
		MarqueeCurrent = MarqueeStart;
		bIsMarqueeDragging = false; // Not dragging yet — wait for threshold
	}
}

void ASeinARTSPlayerController::OnSelectReleased(const FInputActionValue& Value)
{
	bSelectHeld = false;

	if (bIsMarqueeDragging)
	{
		// Marquee select — HUD handles the actor collection via ReceiveMarqueeSelection
		bIsMarqueeDragging = false;
		return;
	}

	// Single-click selection
	FHitResult Hit;
	if (!TraceUnderCursor(Hit))
	{
		if (!bShiftHeld && !bCtrlHeld)
		{
			ClearSelection();
		}
		return;
	}

	ASeinActor* HitActor = GetSeinActorFromHit(Hit);
	if (!HitActor)
	{
		if (!bShiftHeld && !bCtrlHeld)
		{
			ClearSelection();
		}
		return;
	}

	// Ownership check — only select own entities
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem();
	if (Subsystem && HitActor->HasValidEntity())
	{
		FSeinPlayerID OwnerID = Subsystem->GetEntityOwner(HitActor->GetEntityHandle());
		if (OwnerID != SeinPlayerID)
		{
			// Clicked an enemy — clear selection (unless modifier held)
			if (!bShiftHeld && !bCtrlHeld)
			{
				ClearSelection();
			}
			return;
		}
	}

	if (bCtrlHeld)
	{
		ToggleSelection(HitActor);
	}
	else if (bShiftHeld)
	{
		AddToSelection({HitActor});
	}
	else
	{
		SetSelection({HitActor});
	}
}

void ASeinARTSPlayerController::OnCommandStarted(const FInputActionValue& Value)
{
	bRMBHeld = true;
	bIsCommandDragging = false;

	// Record start screen pos for drag threshold
	float MouseX, MouseY;
	if (GetMousePosition(MouseX, MouseY))
	{
		CommandDragScreenStart = FVector2D(MouseX, MouseY);
	}

	// Record world hit position and target actor at press time
	FHitResult Hit;
	if (TraceUnderCursor(Hit))
	{
		CommandDragStart = Hit.ImpactPoint;
		CommandDragCurrent = CommandDragStart;
		CommandTargetActor = GetSeinActorFromHit(Hit);
	}
	else
	{
		CommandDragStart = FVector::ZeroVector;
		CommandDragCurrent = FVector::ZeroVector;
		CommandTargetActor = nullptr;
	}
}

void ASeinARTSPlayerController::OnCommandReleased(const FInputActionValue& Value)
{
	bRMBHeld = false;

	// Get final world position under cursor
	FHitResult Hit;
	FVector FinalLocation = CommandDragStart;
	ASeinActor* TargetActor = CommandTargetActor.Get();

	if (TraceUnderCursor(Hit))
	{
		FinalLocation = Hit.ImpactPoint;
		// If not dragging, use whatever is under cursor at release time
		if (!bIsCommandDragging)
		{
			TargetActor = GetSeinActorFromHit(Hit);
		}
	}

	if (SelectedActors.IsEmpty())
	{
		// Nothing selected — still fire the delegate for feedback (audio cue, etc.)
		OnCommandIssued.Broadcast(FGameplayTag(), FinalLocation);
		bIsCommandDragging = false;
		return;
	}

	if (bIsCommandDragging)
	{
		// Formation drag order: start → end defines formation line
		IssueSmartCommandEx(CommandDragStart, TargetActor, bShiftHeld, FinalLocation);
	}
	else
	{
		// Simple click command
		IssueSmartCommandEx(FinalLocation, TargetActor, bShiftHeld, FVector::ZeroVector);
	}

	bIsCommandDragging = false;
}

void ASeinARTSPlayerController::OnCameraPan(const FInputActionValue& Value)
{
	if (ASeinCameraPawn* CamPawn = Cast<ASeinCameraPawn>(GetPawn()))
	{
		const float Speed = InputConfig ? InputConfig->KeyPanSpeed : 1.0f;
		CamPawn->HandlePanInput(Value.Get<FVector2D>() * Speed);
	}
}

void ASeinARTSPlayerController::OnCameraRotate(const FInputActionValue& Value)
{
	if (ASeinCameraPawn* CamPawn = Cast<ASeinCameraPawn>(GetPawn()))
	{
		const float Speed = InputConfig ? InputConfig->KeyRotateSpeed : 1.0f;
		CamPawn->HandleRotateInput(Value.Get<float>() * Speed);
	}
}

void ASeinARTSPlayerController::OnCameraZoom(const FInputActionValue& Value)
{
	if (ASeinCameraPawn* CamPawn = Cast<ASeinCameraPawn>(GetPawn()))
	{
		const float Speed = InputConfig ? InputConfig->MouseZoomSpeed : 1.0f;
		CamPawn->HandleZoomInput(Value.Get<float>() * Speed);
	}
}

void ASeinARTSPlayerController::OnCameraZoomKeyboard(const FInputActionValue& Value)
{
	if (ASeinCameraPawn* CamPawn = Cast<ASeinCameraPawn>(GetPawn()))
	{
		const float Speed = InputConfig ? InputConfig->KeyZoomSpeed : 1.0f;
		CamPawn->HandleZoomInput(Value.Get<float>() * Speed);
	}
}

void ASeinARTSPlayerController::OnCameraFollowPressed(const FInputActionValue& Value)
{
	ASeinCameraPawn* CamPawn = Cast<ASeinCameraPawn>(GetPawn());
	if (!CamPawn)
	{
		return;
	}

	if (CamPawn->IsFollowing())
	{
		// Toggle off — stop following
		CamPawn->StopFollowing();
		return;
	}

	// Follow the focused entity, or the first selected entity
	ASeinActor* Target = GetFocusedActor();
	if (!Target && SelectedActors.Num() > 0)
	{
		Target = SelectedActors[0].Get();
	}

	if (Target && Target->HasValidEntity())
	{
		CamPawn->FollowEntity(Target->GetEntityHandle());
	}
}

void ASeinARTSPlayerController::OnCameraResetPressed(const FInputActionValue& Value)
{
	if (ASeinCameraPawn* CamPawn = Cast<ASeinCameraPawn>(GetPawn()))
	{
		CamPawn->ResetRotation();
	}
}

void ASeinARTSPlayerController::OnCameraMMBPan(const FInputActionValue& Value)
{
	// Block MMB pan while LMB (select) or RMB (command) are held —
	// those use mouse delta for marquee/formation drag, not camera control.
	if (bSelectHeld || bRMBHeld)
	{
		return;
	}

	if (ASeinCameraPawn* CamPawn = Cast<ASeinCameraPawn>(GetPawn()))
	{
		const float Speed = InputConfig ? InputConfig->MousePanSpeed : 1.0f;
		CamPawn->HandleMMBPanInput(Value.Get<FVector2D>() * Speed);
	}
}

void ASeinARTSPlayerController::OnCameraAltRotate(const FInputActionValue& Value)
{
	// Block orbit while LMB (select) or RMB (command) are held.
	if (bSelectHeld || bRMBHeld)
	{
		return;
	}

	if (ASeinCameraPawn* CamPawn = Cast<ASeinCameraPawn>(GetPawn()))
	{
		const float Speed = InputConfig ? InputConfig->MouseRotateSpeed : 1.0f;
		// IA_MouseRotate is Axis2D: X = yaw, Y = pitch tilt
		CamPawn->HandleOrbitInput(Value.Get<FVector2D>() * Speed);
	}
}

void ASeinARTSPlayerController::OnPingPressed(const FInputActionValue& Value)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem();
	if (!Subsystem)
	{
		return;
	}

	FHitResult Hit;
	if (!TraceUnderCursor(Hit))
	{
		return;
	}

	const FVector PingLocation = Hit.ImpactPoint;
	ASeinActor* HitActor = GetSeinActorFromHit(Hit);
	const FSeinEntityHandle PingTarget = (HitActor && HitActor->HasValidEntity())
		? HitActor->GetEntityHandle()
		: FSeinEntityHandle::Invalid();

	// Enqueue the sim command
	const FFixedVector FixedLocation = FFixedVector::FromVector(PingLocation);
	FSeinCommand Cmd = FSeinCommand::MakePingCommand(SeinPlayerID, FixedLocation, PingTarget);
	Cmd.Tick = Subsystem->GetCurrentTick();
	Subsystem->EnqueueCommand(Cmd);

	// --- Immediate visual feedback (render-side only) ---
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float PingDisplayTime = 3.0f;
	const FColor PingColor = FColor::Magenta;

	// Debug point at ping location
	DrawDebugPoint(World, PingLocation, 10.0f, PingColor, false, PingDisplayTime);

	// Build label
	FString PingLabel;
	if (HitActor)
	{
		PingLabel = FString::Printf(TEXT("Ping %s"), *HitActor->GetActorNameOrLabel());
	}
	else
	{
		PingLabel = FString::Printf(TEXT("Ping %.0f, %.0f, %.0f"),
			PingLocation.X, PingLocation.Y, PingLocation.Z);
	}

	// Debug text above the point
	const FVector TextLocation = PingLocation + FVector(0.0f, 0.0f, 50.0f);
	DrawDebugString(World, TextLocation, PingLabel, nullptr, PingColor, PingDisplayTime, true, 1.5f);
}

void ASeinARTSPlayerController::OnCycleFocusPressed(const FInputActionValue& Value)
{
	CycleFocus();
}

void ASeinARTSPlayerController::OnModifierShift(const FInputActionValue& Value)
{
	bShiftHeld = Value.Get<bool>();
}

void ASeinARTSPlayerController::OnModifierCtrl(const FInputActionValue& Value)
{
	bCtrlHeld = Value.Get<bool>();
}

void ASeinARTSPlayerController::OnModifierAlt(const FInputActionValue& Value)
{
	bAltHeld = Value.Get<bool>();
}

void ASeinARTSPlayerController::HandleControlGroup(int32 GroupIndex)
{
	if (bCtrlHeld)
	{
		AssignControlGroup(GroupIndex);
	}
	else
	{
		RecallControlGroup(GroupIndex);
	}
}

// ==================== Action Slots & Menu ====================

void ASeinARTSPlayerController::HandleActionSlot(int32 SlotIndex)
{
	OnActionSlotPressed.Broadcast(SlotIndex);
}

void ASeinARTSPlayerController::OnMenuKeyPressed(const FInputActionValue& Value)
{
	OnMenuPressed.Broadcast();
}

// ==================== Selection ====================

void ASeinARTSPlayerController::SetSelection(const TArray<ASeinActor*>& NewSelection)
{
	// Deselect old
	for (const TWeakObjectPtr<ASeinActor>& Weak : SelectedActors)
	{
		if (ASeinActor* Actor = Weak.Get())
		{
			if (USeinSelectionComponent* Sel = Actor->FindComponentByClass<USeinSelectionComponent>())
			{
				Sel->SetSelected(false);
			}
		}
	}

	// Build new selection
	SelectedActors.Reset();
	for (ASeinActor* Actor : NewSelection)
	{
		if (Actor && Actor->HasValidEntity())
		{
			SelectedActors.AddUnique(Actor);
		}
	}

	// Select new
	for (const TWeakObjectPtr<ASeinActor>& Weak : SelectedActors)
	{
		if (ASeinActor* Actor = Weak.Get())
		{
			if (USeinSelectionComponent* Sel = Actor->FindComponentByClass<USeinSelectionComponent>())
			{
				Sel->SetSelected(true);
			}
		}
	}

	// Reset focus to "All"
	ActiveFocusIndex = -1;

	NotifySelectionUpdated();
}

void ASeinARTSPlayerController::AddToSelection(const TArray<ASeinActor*>& ActorsToAdd)
{
	for (ASeinActor* Actor : ActorsToAdd)
	{
		if (!Actor || !Actor->HasValidEntity())
		{
			continue;
		}

		// Skip duplicates
		bool bAlreadySelected = false;
		for (const TWeakObjectPtr<ASeinActor>& Weak : SelectedActors)
		{
			if (Weak.Get() == Actor)
			{
				bAlreadySelected = true;
				break;
			}
		}

		if (!bAlreadySelected)
		{
			SelectedActors.Add(Actor);
			if (USeinSelectionComponent* Sel = Actor->FindComponentByClass<USeinSelectionComponent>())
			{
				Sel->SetSelected(true);
			}
		}
	}

	NotifySelectionUpdated();
}

void ASeinARTSPlayerController::ToggleSelection(ASeinActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	// Check if already selected
	for (int32 i = SelectedActors.Num() - 1; i >= 0; --i)
	{
		if (SelectedActors[i].Get() == Actor)
		{
			// Deselect
			if (USeinSelectionComponent* Sel = Actor->FindComponentByClass<USeinSelectionComponent>())
			{
				Sel->SetSelected(false);
			}
			SelectedActors.RemoveAt(i);

			// Reset focus if it pointed at or past the removed index
			if (ActiveFocusIndex >= SelectedActors.Num())
			{
				ActiveFocusIndex = -1;
			}

			NotifySelectionUpdated();
			return;
		}
	}

	// Not in selection — add it
	AddToSelection({Actor});
}

void ASeinARTSPlayerController::ClearSelection()
{
	for (const TWeakObjectPtr<ASeinActor>& Weak : SelectedActors)
	{
		if (ASeinActor* Actor = Weak.Get())
		{
			if (USeinSelectionComponent* Sel = Actor->FindComponentByClass<USeinSelectionComponent>())
			{
				Sel->SetSelected(false);
			}
		}
	}

	SelectedActors.Reset();
	ActiveFocusIndex = -1;

	NotifySelectionUpdated();
}

void ASeinARTSPlayerController::CycleFocus()
{
	if (SelectedActors.IsEmpty())
	{
		ActiveFocusIndex = -1;
		return;
	}

	// Cycle: -1 → 0 → 1 → ... → N-1 → -1
	ActiveFocusIndex++;
	if (ActiveFocusIndex >= SelectedActors.Num())
	{
		ActiveFocusIndex = -1;
	}

	// Log focus change as observer command
	LogSelectionChanged();

	OnSelectionChanged.Broadcast();
}

ASeinActor* ASeinARTSPlayerController::GetFocusedActor() const
{
	if (ActiveFocusIndex >= 0 && ActiveFocusIndex < SelectedActors.Num())
	{
		return SelectedActors[ActiveFocusIndex].Get();
	}
	return nullptr;
}

TArray<ASeinActor*> ASeinARTSPlayerController::GetValidSelectedActors()
{
	TArray<ASeinActor*> Valid;
	Valid.Reserve(SelectedActors.Num());

	for (const TWeakObjectPtr<ASeinActor>& Weak : SelectedActors)
	{
		ASeinActor* Actor = Weak.Get();
		if (Actor && Actor->HasValidEntity())
		{
			Valid.Add(Actor);
		}
	}

	return Valid;
}

// ==================== Control Groups ====================

void ASeinARTSPlayerController::AssignControlGroup(int32 GroupIndex)
{
	if (GroupIndex < 0 || GroupIndex >= 10)
	{
		return;
	}

	ControlGroups[GroupIndex].Reset();
	for (const TWeakObjectPtr<ASeinActor>& Weak : SelectedActors)
	{
		if (ASeinActor* Actor = Weak.Get())
		{
			if (Actor->HasValidEntity())
			{
				ControlGroups[GroupIndex].Add(Actor->GetEntityHandle());
			}
		}
	}
}

void ASeinARTSPlayerController::RecallControlGroup(int32 GroupIndex)
{
	if (GroupIndex < 0 || GroupIndex >= 10)
	{
		return;
	}

	USeinWorldSubsystem* Subsystem = GetWorldSubsystem();
	if (!Subsystem)
	{
		return;
	}

	// Resolve entity handles back to actors
	TArray<ASeinActor*> Actors;
	for (const FSeinEntityHandle& Handle : ControlGroups[GroupIndex])
	{
		if (!Subsystem->IsEntityAlive(Handle))
		{
			continue;
		}

		// Find the actor for this handle by iterating world actors
		// (In production, the world subsystem would maintain a handle→actor map)
		for (TActorIterator<ASeinActor> It(GetWorld()); It; ++It)
		{
			if (It->GetEntityHandle() == Handle)
			{
				Actors.Add(*It);
				break;
			}
		}
	}

	if (Actors.Num() > 0)
	{
		SetSelection(Actors);
	}
}

TArray<FSeinEntityHandle> ASeinARTSPlayerController::GetControlGroup(int32 GroupIndex) const
{
	if (GroupIndex < 0 || GroupIndex >= 10)
	{
		return TArray<FSeinEntityHandle>();
	}
	return ControlGroups[GroupIndex];
}

int32 ASeinARTSPlayerController::GetControlGroupCount(int32 GroupIndex) const
{
	if (GroupIndex < 0 || GroupIndex >= 10)
	{
		return 0;
	}
	return ControlGroups[GroupIndex].Num();
}

// ==================== Marquee Selection ====================

void ASeinARTSPlayerController::ReceiveMarqueeSelection(const TArray<ASeinActor*>& ActorsInBox)
{
	// Filter to owned entities
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem();
	TArray<ASeinActor*> OwnedActors;

	for (ASeinActor* Actor : ActorsInBox)
	{
		if (!Actor || !Actor->HasValidEntity())
		{
			continue;
		}

		if (Subsystem)
		{
			FSeinPlayerID OwnerID = Subsystem->GetEntityOwner(Actor->GetEntityHandle());
			if (OwnerID != SeinPlayerID)
			{
				continue;
			}
		}

		OwnedActors.Add(Actor);
	}

	if (bShiftHeld)
	{
		AddToSelection(OwnedActors);
	}
	else
	{
		SetSelection(OwnedActors);
	}
}

// ==================== Command Resolution ====================

FGameplayTagContainer ASeinARTSPlayerController::BuildCommandContext_Implementation(
	ASeinActor* HitActor, const FVector& HitLocation) const
{
	FGameplayTagContainer Context;

	// Base context
	Context.AddTag(FGameplayTag::RequestGameplayTag(FName("CommandContext.RightClick")));

	if (!HitActor || !HitActor->HasValidEntity())
	{
		// Ground click
		Context.AddTag(FGameplayTag::RequestGameplayTag(FName("CommandContext.Target.Ground")));
		return Context;
	}

	// We have a target actor — determine relationship
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem();
	if (!Subsystem)
	{
		Context.AddTag(FGameplayTag::RequestGameplayTag(FName("CommandContext.Target.Ground")));
		return Context;
	}

	const FSeinPlayerID TargetOwner = Subsystem->GetEntityOwner(HitActor->GetEntityHandle());

	if (TargetOwner == SeinPlayerID)
	{
		Context.AddTag(FGameplayTag::RequestGameplayTag(FName("CommandContext.Target.Friendly")));
	}
	else if (TargetOwner.IsNeutral())
	{
		// Neutral entities (resources, capture points)
		Context.AddTag(FGameplayTag::RequestGameplayTag(FName("CommandContext.Target.Neutral")));
	}
	else
	{
		Context.AddTag(FGameplayTag::RequestGameplayTag(FName("CommandContext.Target.Enemy")));
	}

	// Add entity-specific context tags by checking the target's gameplay tags
	// (e.g., if the target has Unit.Building, add CommandContext.Target.Building)
	// This is extensible — designers can add custom tags via the tag component.

	return Context;
}

void ASeinARTSPlayerController::IssueSmartCommand(const FVector& WorldLocation, ASeinActor* TargetActor)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem();
	if (!Subsystem)
	{
		return;
	}

	// Build the command context from what was clicked
	const FGameplayTagContainer Context = BuildCommandContext(TargetActor, WorldLocation);

	// Determine which entities receive commands
	TArray<ASeinActor*> CommandTargets;
	if (ActiveFocusIndex >= 0 && ActiveFocusIndex < SelectedActors.Num())
	{
		// Focused mode — only the focused entity
		if (ASeinActor* Focused = SelectedActors[ActiveFocusIndex].Get())
		{
			if (Focused->HasValidEntity())
			{
				CommandTargets.Add(Focused);
			}
		}
	}
	else
	{
		// "All" mode — all selected entities
		CommandTargets = GetValidSelectedActors();
	}

	if (CommandTargets.IsEmpty())
	{
		return;
	}

	// Resolve the target entity handle (if clicking on an entity)
	const FSeinEntityHandle TargetEntityHandle =
		(TargetActor && TargetActor->HasValidEntity())
			? TargetActor->GetEntityHandle()
			: FSeinEntityHandle::Invalid();

	// Convert world location to fixed-point
	const FFixedVector FixedLocation = FFixedVector::FromVector(WorldLocation);

	// Resolve the leader's ability tag (for LeaderDriven mode)
	FGameplayTag LeaderAbilityTag;
	if (DispatchMode == ESeinCommandDispatchMode::LeaderDriven && CommandTargets.Num() > 0)
	{
		if (const USeinArchetypeDefinition* LeaderArchetype = CommandTargets[0]->ArchetypeDefinition)
		{
			LeaderAbilityTag = LeaderArchetype->ResolveCommandContext(Context);
		}
	}

	FGameplayTag LastIssuedTag;

	for (ASeinActor* Actor : CommandTargets)
	{
		if (!Actor || !Actor->HasValidEntity())
		{
			continue;
		}

		FGameplayTag AbilityTag;

		if (DispatchMode == ESeinCommandDispatchMode::PerEntity)
		{
			// Each entity resolves its own command
			if (const USeinArchetypeDefinition* Archetype = Actor->ArchetypeDefinition)
			{
				AbilityTag = Archetype->ResolveCommandContext(Context);
			}
		}
		else // LeaderDriven
		{
			if (Actor == CommandTargets[0])
			{
				// Leader uses its own resolved tag
				AbilityTag = LeaderAbilityTag;
			}
			else
			{
				// Followers: try to use the leader's ability tag.
				// Check if this entity has that ability — if not, fall back.
				if (const USeinArchetypeDefinition* Archetype = Actor->ArchetypeDefinition)
				{
					// Check if this entity can execute the leader's ability
					// by seeing if any of its mappings produce the same tag
					const FGameplayTag OwnTag = Archetype->ResolveCommandContext(Context);
					if (OwnTag == LeaderAbilityTag)
					{
						AbilityTag = LeaderAbilityTag;
					}
					else
					{
						// Can't do the same thing — fall back to this entity's fallback (usually Move)
						AbilityTag = Archetype->FallbackAbilityTag;
					}
				}
			}
		}

		if (!AbilityTag.IsValid())
		{
			continue;
		}

		// Create and enqueue the command
		FSeinCommand Cmd = FSeinCommand::MakeAbilityCommand(
			SeinPlayerID,
			Actor->GetEntityHandle(),
			AbilityTag,
			TargetEntityHandle,
			FixedLocation
		);
		Cmd.Tick = Subsystem->GetCurrentTick();

		Subsystem->EnqueueCommand(Cmd);
		LastIssuedTag = AbilityTag;
	}

	// Fire feedback event
	if (LastIssuedTag.IsValid())
	{
		OnCommandIssued.Broadcast(LastIssuedTag, WorldLocation);
	}
}

void ASeinARTSPlayerController::IssueSmartCommandEx(
	const FVector& WorldLocation, ASeinActor* TargetActor, bool bQueue, const FVector& FormationEnd)
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem();
	if (!Subsystem)
	{
		return;
	}

	const FGameplayTagContainer Context = BuildCommandContext(TargetActor, WorldLocation);

	// Determine which entities receive commands
	TArray<ASeinActor*> CommandTargets;
	if (ActiveFocusIndex >= 0 && ActiveFocusIndex < SelectedActors.Num())
	{
		if (ASeinActor* Focused = SelectedActors[ActiveFocusIndex].Get())
		{
			if (Focused->HasValidEntity())
			{
				CommandTargets.Add(Focused);
			}
		}
	}
	else
	{
		CommandTargets = GetValidSelectedActors();
	}

	if (CommandTargets.IsEmpty())
	{
		return;
	}

	const FSeinEntityHandle TargetEntityHandle =
		(TargetActor && TargetActor->HasValidEntity())
			? TargetActor->GetEntityHandle()
			: FSeinEntityHandle::Invalid();

	const FFixedVector FixedLocation = FFixedVector::FromVector(WorldLocation);
	const FFixedVector FixedFormationEnd = FormationEnd.IsNearlyZero()
		? FFixedVector()
		: FFixedVector::FromVector(FormationEnd);

	// Resolve leader tag for LeaderDriven mode
	FGameplayTag LeaderAbilityTag;
	if (DispatchMode == ESeinCommandDispatchMode::LeaderDriven && CommandTargets.Num() > 0)
	{
		if (const USeinArchetypeDefinition* LeaderArchetype = CommandTargets[0]->ArchetypeDefinition)
		{
			LeaderAbilityTag = LeaderArchetype->ResolveCommandContext(Context);
		}
	}

	FGameplayTag LastIssuedTag;

	for (ASeinActor* Actor : CommandTargets)
	{
		if (!Actor || !Actor->HasValidEntity())
		{
			continue;
		}

		FGameplayTag AbilityTag;

		if (DispatchMode == ESeinCommandDispatchMode::PerEntity)
		{
			if (const USeinArchetypeDefinition* Archetype = Actor->ArchetypeDefinition)
			{
				AbilityTag = Archetype->ResolveCommandContext(Context);
			}
		}
		else // LeaderDriven
		{
			if (Actor == CommandTargets[0])
			{
				AbilityTag = LeaderAbilityTag;
			}
			else
			{
				if (const USeinArchetypeDefinition* Archetype = Actor->ArchetypeDefinition)
				{
					const FGameplayTag OwnTag = Archetype->ResolveCommandContext(Context);
					if (OwnTag == LeaderAbilityTag)
					{
						AbilityTag = LeaderAbilityTag;
					}
					else
					{
						AbilityTag = Archetype->FallbackAbilityTag;
					}
				}
			}
		}

		if (!AbilityTag.IsValid())
		{
			continue;
		}

		FSeinCommand Cmd = FSeinCommand::MakeAbilityCommandEx(
			SeinPlayerID,
			Actor->GetEntityHandle(),
			AbilityTag,
			TargetEntityHandle,
			FixedLocation,
			bQueue,
			FixedFormationEnd
		);
		Cmd.Tick = Subsystem->GetCurrentTick();

		Subsystem->EnqueueCommand(Cmd);
		LastIssuedTag = AbilityTag;
	}

	if (LastIssuedTag.IsValid())
	{
		OnCommandIssued.Broadcast(LastIssuedTag, WorldLocation);
	}
}

// ==================== Internal Helpers ====================

bool ASeinARTSPlayerController::TraceUnderCursor(FHitResult& OutHit) const
{
	float MouseX, MouseY;
	if (!GetMousePosition(MouseX, MouseY))
	{
		return false;
	}

	FVector WorldOrigin, WorldDirection;
	if (!DeprojectScreenPositionToWorld(MouseX, MouseY, WorldOrigin, WorldDirection))
	{
		return false;
	}

	const FVector TraceEnd = WorldOrigin + WorldDirection * TraceDistance;

	FCollisionQueryParams Params;
	Params.bTraceComplex = false;

	return GetWorld()->LineTraceSingleByChannel(OutHit, WorldOrigin, TraceEnd, SelectionTraceChannel, Params);
}

ASeinActor* ASeinARTSPlayerController::GetSeinActorFromHit(const FHitResult& Hit) const
{
	if (!Hit.GetActor())
	{
		return nullptr;
	}

	return Cast<ASeinActor>(Hit.GetActor());
}

void ASeinARTSPlayerController::UpdateHover()
{
	FHitResult Hit;
	ASeinActor* NewHovered = nullptr;

	if (TraceUnderCursor(Hit))
	{
		NewHovered = GetSeinActorFromHit(Hit);
	}

	// Update marquee dragging — only when IA_Select actually fired (bSelectHeld),
	// not on raw LMB check (which fires even for Ctrl+LMB / SelectAllOfType).
	if (bSelectHeld)
	{
		float MouseX, MouseY;
		if (GetMousePosition(MouseX, MouseY))
		{
			MarqueeCurrent = FVector2D(MouseX, MouseY);

			if (!bIsMarqueeDragging)
			{
				const float DragDist = FVector2D::Distance(MarqueeStart, MarqueeCurrent);
				if (DragDist > MarqueeDragThreshold)
				{
					bIsMarqueeDragging = true;
				}
			}
		}
	}

	// Update hover state
	ASeinActor* OldHovered = HoveredActor.Get();
	if (NewHovered != OldHovered)
	{
		if (OldHovered)
		{
			if (USeinSelectionComponent* Sel = OldHovered->FindComponentByClass<USeinSelectionComponent>())
			{
				Sel->SetHovered(false);
			}
		}

		HoveredActor = NewHovered;

		if (NewHovered)
		{
			if (USeinSelectionComponent* Sel = NewHovered->FindComponentByClass<USeinSelectionComponent>())
			{
				Sel->SetHovered(true);
			}
		}
	}
}

void ASeinARTSPlayerController::UpdateCommandDrag()
{
	if (!bRMBHeld)
	{
		return;
	}

	float MouseX, MouseY;
	if (!GetMousePosition(MouseX, MouseY))
	{
		return;
	}

	// Check drag threshold
	if (!bIsCommandDragging)
	{
		const float DragDist = FVector2D::Distance(CommandDragScreenStart, FVector2D(MouseX, MouseY));
		if (DragDist > CommandDragThreshold)
		{
			bIsCommandDragging = true;
		}
	}

	// Update current drag position in world space
	if (bIsCommandDragging)
	{
		FHitResult Hit;
		if (TraceUnderCursor(Hit))
		{
			CommandDragCurrent = Hit.ImpactPoint;
		}
	}
}

void ASeinARTSPlayerController::PurgeStaleSelection()
{
	bool bChanged = false;

	for (int32 i = SelectedActors.Num() - 1; i >= 0; --i)
	{
		ASeinActor* Actor = SelectedActors[i].Get();
		if (!Actor || !Actor->HasValidEntity())
		{
			SelectedActors.RemoveAt(i);
			bChanged = true;
		}
	}

	if (bChanged)
	{
		// Clamp focus index
		if (ActiveFocusIndex >= SelectedActors.Num())
		{
			ActiveFocusIndex = -1;
		}
		NotifySelectionUpdated();
	}
}

void ASeinARTSPlayerController::LogCameraUpdate()
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem();
	if (!Subsystem || !Subsystem->IsSimulationRunning())
	{
		return;
	}

	const int32 CurrentTick = Subsystem->GetCurrentTick();

	// Throttle: only log every N ticks
	if (CameraLogInterval > 0 && (CurrentTick - LastCameraLogTick) < CameraLogInterval)
	{
		return;
	}

	const ASeinCameraPawn* CamPawn = Cast<ASeinCameraPawn>(GetPawn());
	if (!CamPawn)
	{
		return;
	}

	const FVector PivotPos = CamPawn->GetPivotLocation();
	const float Yaw = CamPawn->GetCameraYaw();
	const float Pitch = CamPawn->GetCameraPitch();
	const float Zoom = CamPawn->GetCurrentZoomDistance();

	// Skip if nothing changed significantly (position, rotation, or zoom)
	if (LastCameraLogTick >= 0)
	{
		const bool bPosMoved = FVector::DistSquared(PivotPos, LastLoggedCameraPos) > 100.0f;
		const bool bYawChanged = FMath::Abs(Yaw - LastLoggedCameraYaw) > 0.5f;
		const bool bPitchChanged = FMath::Abs(Pitch - LastLoggedCameraPitch) > 0.5f;
		const bool bZoomChanged = FMath::Abs(Zoom - LastLoggedCameraZoom) > 5.0f;

		if (!bPosMoved && !bYawChanged && !bPitchChanged && !bZoomChanged)
		{
			return;
		}
	}

	FSeinCommand Cmd = FSeinCommand::MakeCameraUpdateCommand(
		SeinPlayerID,
		FFixedVector::FromVector(PivotPos),
		FFixedPoint::FromFloat(Yaw),
		FFixedPoint::FromFloat(Zoom)
	);
	// Store pitch in AuxLocation.X (unused for CameraUpdate commands)
	Cmd.AuxLocation = FFixedVector(FFixedPoint::FromFloat(Pitch), FFixedPoint::Zero, FFixedPoint::Zero);
	Cmd.Tick = CurrentTick;

	Subsystem->EnqueueCommand(Cmd);

	LastCameraLogTick = CurrentTick;
	LastLoggedCameraPos = PivotPos;
	LastLoggedCameraYaw = Yaw;
	LastLoggedCameraPitch = Pitch;
	LastLoggedCameraZoom = Zoom;
}

void ASeinARTSPlayerController::LogSelectionChanged()
{
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem();
	if (!Subsystem || !Subsystem->IsSimulationRunning())
	{
		return;
	}

	// Build entity handle list from current selection
	TArray<FSeinEntityHandle> Handles;
	Handles.Reserve(SelectedActors.Num());
	for (const TWeakObjectPtr<ASeinActor>& Weak : SelectedActors)
	{
		if (ASeinActor* Actor = Weak.Get())
		{
			if (Actor->HasValidEntity())
			{
				Handles.Add(Actor->GetEntityHandle());
			}
		}
	}

	const FSeinCommand Cmd = FSeinCommand::MakeSelectionChangedCommand(SeinPlayerID, Handles, ActiveFocusIndex);
	Subsystem->EnqueueCommand(Cmd);
}

void ASeinARTSPlayerController::NotifySelectionUpdated()
{
	LogSelectionChanged();
	OnSelectionChanged.Broadcast();
}

USeinWorldSubsystem* ASeinARTSPlayerController::GetWorldSubsystem() const
{
	if (CachedWorldSubsystem.IsValid())
	{
		return CachedWorldSubsystem.Get();
	}

	if (UWorld* World = GetWorld())
	{
		USeinWorldSubsystem* Subsystem = World->GetSubsystem<USeinWorldSubsystem>();
		CachedWorldSubsystem = Subsystem;
		return Subsystem;
	}

	return nullptr;
}
