/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinPlayerController.cpp
 * @brief   RTS player controller implementation.
 */

#include "Player/SeinPlayerController.h"
#include "Player/SeinCameraPawn.h"
#include "Player/SeinSelectionComponent.h"
#include "Input/SeinInputConfig.h"
#include "Actor/SeinActor.h"
#include "Actor/SeinActorBridge.h"
#include "Data/SeinArchetypeDefinition.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Input/SeinCommand.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "DrawDebugHelpers.h"

ASeinPlayerController::ASeinPlayerController()
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

void ASeinPlayerController::BeginPlay()
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

void ASeinPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateHover();
	UpdateCommandDrag();
	PurgeStaleSelection();
	LogCameraUpdate();
}

// ==================== Input Setup ====================

void ASeinPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (!InputConfig)
	{
		UE_LOG(LogTemp, Warning, TEXT("SeinPlayerController: No InputConfig assigned. Input will not function."));
		return;
	}

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EIC)
	{
		UE_LOG(LogTemp, Error, TEXT("SeinPlayerController: InputComponent is not UEnhancedInputComponent. Check project input settings."));
		return;
	}

	// Selection
	if (InputConfig->IA_Select)
	{
		EIC->BindAction(InputConfig->IA_Select, ETriggerEvent::Started, this, &ASeinPlayerController::OnSelectPressed);
		EIC->BindAction(InputConfig->IA_Select, ETriggerEvent::Completed, this, &ASeinPlayerController::OnSelectReleased);
	}

	// Command (fire on release for drag order support)
	if (InputConfig->IA_Command)
	{
		EIC->BindAction(InputConfig->IA_Command, ETriggerEvent::Started, this, &ASeinPlayerController::OnCommandStarted);
		EIC->BindAction(InputConfig->IA_Command, ETriggerEvent::Completed, this, &ASeinPlayerController::OnCommandReleased);
	}

	// Camera — keyboard pan (WASD / arrows)
	if (InputConfig->IA_KeyPan)
	{
		EIC->BindAction(InputConfig->IA_KeyPan, ETriggerEvent::Triggered, this, &ASeinPlayerController::OnCameraPan);
	}
	// Camera — keyboard rotate (Q/E)
	if (InputConfig->IA_KeyRotate)
	{
		EIC->BindAction(InputConfig->IA_KeyRotate, ETriggerEvent::Triggered, this, &ASeinPlayerController::OnCameraRotate);
	}
	// Camera — mouse zoom (scroll wheel)
	if (InputConfig->IA_MouseZoom)
	{
		EIC->BindAction(InputConfig->IA_MouseZoom, ETriggerEvent::Triggered, this, &ASeinPlayerController::OnCameraZoom);
	}
	// Camera — keyboard zoom (Z/X)
	if (InputConfig->IA_KeyZoom)
	{
		EIC->BindAction(InputConfig->IA_KeyZoom, ETriggerEvent::Triggered, this, &ASeinPlayerController::OnCameraZoomKeyboard);
	}
	// Camera — follow (F)
	if (InputConfig->IA_FollowCamera)
	{
		EIC->BindAction(InputConfig->IA_FollowCamera, ETriggerEvent::Started, this, &ASeinPlayerController::OnCameraFollowPressed);
	}
	// Camera — reset rotation (Backspace)
	if (InputConfig->IA_ResetCamera)
	{
		EIC->BindAction(InputConfig->IA_ResetCamera, ETriggerEvent::Started, this, &ASeinPlayerController::OnCameraResetPressed);
	}
	// Camera — MMB pan (chorded in mapping context)
	if (InputConfig->IA_MousePan)
	{
		EIC->BindAction(InputConfig->IA_MousePan, ETriggerEvent::Triggered, this, &ASeinPlayerController::OnCameraMMBPan);
	}
	// Camera — Alt+MMB rotate (chorded in mapping context)
	if (InputConfig->IA_MouseRotate)
	{
		EIC->BindAction(InputConfig->IA_MouseRotate, ETriggerEvent::Triggered, this, &ASeinPlayerController::OnCameraAltRotate);
	}

	// Ping (Ctrl+MMB)
	if (InputConfig->IA_Ping)
	{
		EIC->BindAction(InputConfig->IA_Ping, ETriggerEvent::Started, this, &ASeinPlayerController::OnPingPressed);
	}

	// Focus cycling
	if (InputConfig->IA_CycleFocus)
	{
		EIC->BindAction(InputConfig->IA_CycleFocus, ETriggerEvent::Started, this, &ASeinPlayerController::OnCycleFocusPressed);
	}

	// Modifiers
	if (InputConfig->IA_ModifierShift)
	{
		EIC->BindAction(InputConfig->IA_ModifierShift, ETriggerEvent::Started, this, &ASeinPlayerController::OnModifierShift);
		EIC->BindAction(InputConfig->IA_ModifierShift, ETriggerEvent::Completed, this, &ASeinPlayerController::OnModifierShift);
	}
	if (InputConfig->IA_ModifierCtrl)
	{
		EIC->BindAction(InputConfig->IA_ModifierCtrl, ETriggerEvent::Started, this, &ASeinPlayerController::OnModifierCtrl);
		EIC->BindAction(InputConfig->IA_ModifierCtrl, ETriggerEvent::Completed, this, &ASeinPlayerController::OnModifierCtrl);
	}
	if (InputConfig->IA_ModifierAlt)
	{
		EIC->BindAction(InputConfig->IA_ModifierAlt, ETriggerEvent::Started, this, &ASeinPlayerController::OnModifierAlt);
		EIC->BindAction(InputConfig->IA_ModifierAlt, ETriggerEvent::Completed, this, &ASeinPlayerController::OnModifierAlt);
	}

	// Control groups (0-9) — bind to individual handler methods since BindAction requires member function pointers
	using HandlerFn = void (ASeinPlayerController::*)(const FInputActionValue&);
	static const HandlerFn ControlGroupHandlers[10] = {
		&ASeinPlayerController::OnControlGroup0, &ASeinPlayerController::OnControlGroup1,
		&ASeinPlayerController::OnControlGroup2, &ASeinPlayerController::OnControlGroup3,
		&ASeinPlayerController::OnControlGroup4, &ASeinPlayerController::OnControlGroup5,
		&ASeinPlayerController::OnControlGroup6, &ASeinPlayerController::OnControlGroup7,
		&ASeinPlayerController::OnControlGroup8, &ASeinPlayerController::OnControlGroup9,
	};

	for (int32 i = 0; i < InputConfig->IA_ControlGroups.Num() && i < 10; ++i)
	{
		if (InputConfig->IA_ControlGroups[i])
		{
			EIC->BindAction(InputConfig->IA_ControlGroups[i], ETriggerEvent::Started, this, ControlGroupHandlers[i]);
		}
	}

	// Action slot hotkeys (12 slots for ability/action panel)
	using ActionSlotFn = void (ASeinPlayerController::*)(const FInputActionValue&);
	static const ActionSlotFn ActionSlotHandlers[12] = {
		&ASeinPlayerController::OnActionSlot0,  &ASeinPlayerController::OnActionSlot1,
		&ASeinPlayerController::OnActionSlot2,  &ASeinPlayerController::OnActionSlot3,
		&ASeinPlayerController::OnActionSlot4,  &ASeinPlayerController::OnActionSlot5,
		&ASeinPlayerController::OnActionSlot6,  &ASeinPlayerController::OnActionSlot7,
		&ASeinPlayerController::OnActionSlot8,  &ASeinPlayerController::OnActionSlot9,
		&ASeinPlayerController::OnActionSlot10, &ASeinPlayerController::OnActionSlot11,
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
		EIC->BindAction(InputConfig->IA_Menu, ETriggerEvent::Started, this, &ASeinPlayerController::OnMenuKeyPressed);
	}
}

// ==================== Input Handlers ====================

void ASeinPlayerController::OnSelectPressed(const FInputActionValue& Value)
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

void ASeinPlayerController::OnSelectReleased(const FInputActionValue& Value)
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

void ASeinPlayerController::OnCommandStarted(const FInputActionValue& Value)
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

void ASeinPlayerController::OnCommandReleased(const FInputActionValue& Value)
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

void ASeinPlayerController::OnCameraPan(const FInputActionValue& Value)
{
	if (ASeinCameraPawn* CamPawn = Cast<ASeinCameraPawn>(GetPawn()))
	{
		const float Speed = InputConfig ? InputConfig->KeyPanSpeed : 1.0f;
		CamPawn->HandlePanInput(Value.Get<FVector2D>() * Speed);
	}
}

void ASeinPlayerController::OnCameraRotate(const FInputActionValue& Value)
{
	if (ASeinCameraPawn* CamPawn = Cast<ASeinCameraPawn>(GetPawn()))
	{
		const float Speed = InputConfig ? InputConfig->KeyRotateSpeed : 1.0f;
		CamPawn->HandleRotateInput(Value.Get<float>() * Speed);
	}
}

void ASeinPlayerController::OnCameraZoom(const FInputActionValue& Value)
{
	if (ASeinCameraPawn* CamPawn = Cast<ASeinCameraPawn>(GetPawn()))
	{
		const float Speed = InputConfig ? InputConfig->MouseZoomSpeed : 1.0f;
		CamPawn->HandleZoomInput(Value.Get<float>() * Speed);
	}
}

void ASeinPlayerController::OnCameraZoomKeyboard(const FInputActionValue& Value)
{
	if (ASeinCameraPawn* CamPawn = Cast<ASeinCameraPawn>(GetPawn()))
	{
		const float Speed = InputConfig ? InputConfig->KeyZoomSpeed : 1.0f;
		CamPawn->HandleZoomInput(Value.Get<float>() * Speed);
	}
}

void ASeinPlayerController::OnCameraFollowPressed(const FInputActionValue& Value)
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

void ASeinPlayerController::OnCameraResetPressed(const FInputActionValue& Value)
{
	if (ASeinCameraPawn* CamPawn = Cast<ASeinCameraPawn>(GetPawn()))
	{
		CamPawn->ResetRotation();
	}
}

void ASeinPlayerController::OnCameraMMBPan(const FInputActionValue& Value)
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

void ASeinPlayerController::OnCameraAltRotate(const FInputActionValue& Value)
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

void ASeinPlayerController::OnPingPressed(const FInputActionValue& Value)
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

void ASeinPlayerController::OnCycleFocusPressed(const FInputActionValue& Value)
{
	CycleFocus();
}

void ASeinPlayerController::OnModifierShift(const FInputActionValue& Value)
{
	bShiftHeld = Value.Get<bool>();
}

void ASeinPlayerController::OnModifierCtrl(const FInputActionValue& Value)
{
	bCtrlHeld = Value.Get<bool>();
}

void ASeinPlayerController::OnModifierAlt(const FInputActionValue& Value)
{
	bAltHeld = Value.Get<bool>();
}

void ASeinPlayerController::HandleControlGroup(int32 GroupIndex)
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

void ASeinPlayerController::HandleActionSlot(int32 SlotIndex)
{
	OnActionSlotPressed.Broadcast(SlotIndex);
}

void ASeinPlayerController::OnMenuKeyPressed(const FInputActionValue& Value)
{
	OnMenuPressed.Broadcast();
}

// ==================== Selection ====================

void ASeinPlayerController::SetSelection(const TArray<ASeinActor*>& NewSelection)
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

void ASeinPlayerController::AddToSelection(const TArray<ASeinActor*>& ActorsToAdd)
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

void ASeinPlayerController::ToggleSelection(ASeinActor* Actor)
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

void ASeinPlayerController::ClearSelection()
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

void ASeinPlayerController::CycleFocus()
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

ASeinActor* ASeinPlayerController::GetFocusedActor() const
{
	if (ActiveFocusIndex >= 0 && ActiveFocusIndex < SelectedActors.Num())
	{
		return SelectedActors[ActiveFocusIndex].Get();
	}
	return nullptr;
}

TArray<ASeinActor*> ASeinPlayerController::GetValidSelectedActors()
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

void ASeinPlayerController::AssignControlGroup(int32 GroupIndex)
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

void ASeinPlayerController::RecallControlGroup(int32 GroupIndex)
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

TArray<FSeinEntityHandle> ASeinPlayerController::GetControlGroup(int32 GroupIndex) const
{
	if (GroupIndex < 0 || GroupIndex >= 10)
	{
		return TArray<FSeinEntityHandle>();
	}
	return ControlGroups[GroupIndex];
}

int32 ASeinPlayerController::GetControlGroupCount(int32 GroupIndex) const
{
	if (GroupIndex < 0 || GroupIndex >= 10)
	{
		return 0;
	}
	return ControlGroups[GroupIndex].Num();
}

// ==================== Marquee Selection ====================

void ASeinPlayerController::ReceiveMarqueeSelection(const TArray<ASeinActor*>& ActorsInBox)
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

FGameplayTagContainer ASeinPlayerController::BuildCommandContext_Implementation(
	ASeinActor* HitActor, const FVector& HitLocation) const
{
	FGameplayTagContainer Context;

	// Base context
	Context.AddTag(SeinARTSTags::CommandContext_RightClick);

	if (!HitActor || !HitActor->HasValidEntity())
	{
		// Ground click
		Context.AddTag(SeinARTSTags::CommandContext_Target_Ground);
		return Context;
	}

	// We have a target actor — determine relationship
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem();
	if (!Subsystem)
	{
		Context.AddTag(SeinARTSTags::CommandContext_Target_Ground);
		return Context;
	}

	const FSeinPlayerID TargetOwner = Subsystem->GetEntityOwner(HitActor->GetEntityHandle());

	if (TargetOwner == SeinPlayerID)
	{
		Context.AddTag(SeinARTSTags::CommandContext_Target_Friendly);
	}
	else if (TargetOwner.IsNeutral())
	{
		// Neutral entities (resources, capture points)
		Context.AddTag(SeinARTSTags::CommandContext_Target_Neutral);
	}
	else
	{
		Context.AddTag(SeinARTSTags::CommandContext_Target_Enemy);
	}

	// Add entity-specific context tags by checking the target's gameplay tags
	// (e.g., if the target has Unit.Building, add CommandContext.Target.Building)
	// This is extensible — designers can add custom tags via the tag component.

	return Context;
}

void ASeinPlayerController::IssueSmartCommand(const FVector& WorldLocation, ASeinActor* TargetActor)
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

void ASeinPlayerController::IssueSmartCommandEx(
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

bool ASeinPlayerController::TraceUnderCursor(FHitResult& OutHit) const
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

ASeinActor* ASeinPlayerController::GetSeinActorFromHit(const FHitResult& Hit) const
{
	if (!Hit.GetActor())
	{
		return nullptr;
	}

	return Cast<ASeinActor>(Hit.GetActor());
}

void ASeinPlayerController::UpdateHover()
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

void ASeinPlayerController::UpdateCommandDrag()
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

void ASeinPlayerController::PurgeStaleSelection()
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

void ASeinPlayerController::LogCameraUpdate()
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

void ASeinPlayerController::LogSelectionChanged()
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

void ASeinPlayerController::NotifySelectionUpdated()
{
	LogSelectionChanged();
	OnSelectionChanged.Broadcast();
}

USeinWorldSubsystem* ASeinPlayerController::GetWorldSubsystem() const
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
