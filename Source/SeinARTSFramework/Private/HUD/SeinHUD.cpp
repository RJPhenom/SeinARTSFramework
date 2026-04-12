/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinHUD.cpp
 * @brief   RTS HUD implementation — marquee box, command drag, debug log panel.
 */

#include "HUD/SeinHUD.h"
#include "Player/SeinPlayerController.h"
#include "Debug/SeinCommandLogSubsystem.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Actor/SeinActor.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Blueprint/UserWidget.h"
#include "CanvasItem.h"
#include "DrawDebugHelpers.h"

void ASeinHUD::BeginPlay()
{
	Super::BeginPlay();

	if (HUDLayoutWidgetClass)
	{
		APlayerController* PC = GetOwningPlayerController();
		if (PC)
		{
			HUDLayoutWidget = CreateWidget<UUserWidget>(PC, HUDLayoutWidgetClass);
			if (HUDLayoutWidget)
			{
				HUDLayoutWidget->AddToViewport();
			}
		}
	}
}

void ASeinHUD::DrawHUD()
{
	Super::DrawHUD();

	ASeinPlayerController* PC = GetSeinPlayerController();
	if (!PC)
	{
		return;
	}

	if (PC->bIsMarqueeDragging)
	{
		DrawMarqueeBox();
		bMarqueeWasActive = true;
	}

	// Draw command drag formation line
	if (PC->bIsCommandDragging)
	{
		DrawCommandDragLine();
	}

	if (!PC->bIsMarqueeDragging && bMarqueeWasActive)
	{
		// Marquee just ended — resolve selection
		ResolveMarqueeSelection();
		bMarqueeWasActive = false;
	}

	// Debug command log overlay
	DrawCommandLogPanel();
}

void ASeinHUD::DrawMarqueeBox()
{
	const ASeinPlayerController* PC = GetSeinPlayerController();
	if (!PC)
	{
		return;
	}

	const FVector2D Start = PC->MarqueeStart;
	const FVector2D Current = PC->MarqueeCurrent;

	const float MinX = FMath::Min(Start.X, Current.X);
	const float MinY = FMath::Min(Start.Y, Current.Y);
	const float MaxX = FMath::Max(Start.X, Current.X);
	const float MaxY = FMath::Max(Start.Y, Current.Y);
	const float Width = MaxX - MinX;
	const float Height = MaxY - MinY;

	// Draw filled rectangle
	DrawRect(MarqueeFillColor, MinX, MinY, Width, Height);

	// Draw border (four lines)
	const float T = MarqueeBorderThickness;
	const FLinearColor& C = MarqueeBorderColor;

	DrawRect(C, MinX, MinY, Width, T);         // Top
	DrawRect(C, MinX, MaxY - T, Width, T);     // Bottom
	DrawRect(C, MinX, MinY, T, Height);         // Left
	DrawRect(C, MaxX - T, MinY, T, Height);     // Right
}

void ASeinHUD::ResolveMarqueeSelection()
{
	ASeinPlayerController* PC = GetSeinPlayerController();
	if (!PC)
	{
		return;
	}

	const FVector2D Start = PC->MarqueeStart;
	const FVector2D End = PC->MarqueeCurrent;

	TArray<ASeinActor*> ActorsInBox;
	GetActorsInSelectionRectangle<ASeinActor>(Start, End, ActorsInBox, false, false);

	PC->ReceiveMarqueeSelection(ActorsInBox);
}

void ASeinHUD::DrawCommandDragLine()
{
	const ASeinPlayerController* PC = GetSeinPlayerController();
	if (!PC)
	{
		return;
	}

	const FVector& WorldStart = PC->CommandDragStart;
	const FVector& WorldEnd = PC->CommandDragCurrent;

	// Screen-space formation line
	FVector2D ScreenStart, ScreenEnd;
	if (!PC->ProjectWorldLocationToScreen(WorldStart, ScreenStart))
	{
		return;
	}
	if (!PC->ProjectWorldLocationToScreen(WorldEnd, ScreenEnd))
	{
		return;
	}

	if (Canvas)
	{
		FCanvasLineItem LineItem(FVector2D(ScreenStart.X, ScreenStart.Y), FVector2D(ScreenEnd.X, ScreenEnd.Y));
		LineItem.SetColor(CommandDragLineColor);
		LineItem.LineThickness = CommandDragLineThickness;
		Canvas->DrawItem(LineItem);
	}

	// World-space formation facing arrow at the midpoint.
	// The formation line defines the line units spread along.
	// The facing direction is perpendicular to this (cross with world up).
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector FormationDir = WorldEnd - WorldStart;
	const float FormationLength = FormationDir.Size();
	if (FormationLength < 10.0f)
	{
		return; // Too short to determine facing
	}

	const FVector FormationDirNorm = FormationDir / FormationLength;
	// Perpendicular facing direction: cross formation line with world up.
	// This gives the "forward" direction the army should face.
	const FVector FacingDir = FVector::CrossProduct(FormationDirNorm, FVector::UpVector).GetSafeNormal();

	const FVector Midpoint = (WorldStart + WorldEnd) * 0.5f;
	const float ArrowLength = FMath::Clamp(FormationLength * 0.15f, 30.0f, 120.0f);

	DrawDebugDirectionalArrow(World, Midpoint, Midpoint + FacingDir * ArrowLength,
		40.0f, FColor(0, 255, 80), false, 0.0f, SDPG_World, 8.0f);
}

// ==================== Debug Command Log Panel ====================

void ASeinHUD::DrawCommandLogPanel()
{
	UWorld* World = GetWorld();
	if (!World || !Canvas)
	{
		return;
	}

	USeinCommandLogSubsystem* LogSub = World->GetSubsystem<USeinCommandLogSubsystem>();
	if (!LogSub || !LogSub->bShowOverlay)
	{
		return;
	}

	const TArray<FSeinCommandLogEntry>& Entries = LogSub->GetLogEntries();
	const int32 MaxDisplay = LogSub->MaxDisplayEntries;

	// Get the default engine font
	UFont* Font = GEngine->GetSmallFont();
	if (!Font)
	{
		return;
	}

	const float FontScale = LogFontScale;
	const float LineHeight = Font->GetMaxCharHeight() * FontScale + 2.0f;

	// Panel dimensions
	const float PanelWidth = Canvas->ClipX * 0.45f;  // 45% of screen width
	const float PanelPadding = 8.0f;
	const float HeaderHeight = LineHeight + 4.0f;

	// How many entries to show
	const int32 StartIndex = FMath::Max(0, Entries.Num() - MaxDisplay);
	const int32 EntryCount = Entries.Num() - StartIndex;
	const float PanelHeight = HeaderHeight + (EntryCount * LineHeight) + PanelPadding * 2.0f;

	// Position: top-left with margin
	const float PanelX = 10.0f;
	const float PanelY = 10.0f;

	// Draw background
	DrawRect(LogPanelBgColor, PanelX, PanelY, PanelWidth, PanelHeight);

	// Draw border
	const FLinearColor BorderColor(0.3f, 0.6f, 1.0f, 0.6f);
	const float BT = 1.0f;
	DrawRect(BorderColor, PanelX, PanelY, PanelWidth, BT);                         // Top
	DrawRect(BorderColor, PanelX, PanelY + PanelHeight - BT, PanelWidth, BT);      // Bottom
	DrawRect(BorderColor, PanelX, PanelY, BT, PanelHeight);                         // Left
	DrawRect(BorderColor, PanelX + PanelWidth - BT, PanelY, BT, PanelHeight);       // Right

	// Header
	const USeinWorldSubsystem* SimSub = World->GetSubsystem<USeinWorldSubsystem>();
	const int32 CurrentTick = SimSub ? SimSub->GetCurrentTick() : -1;

	const FString Header = FString::Printf(TEXT(" COMMAND LOG  |  Tick %d  |  %d entries"),
		CurrentTick, Entries.Num());

	float TextX = PanelX + PanelPadding;
	float TextY = PanelY + PanelPadding;

	// Draw header text
	{
		FCanvasTextItem HeaderItem(FVector2D(TextX, TextY), FText::FromString(Header), Font, FLinearColor(0.4f, 0.8f, 1.0f, 1.0f));
		HeaderItem.Scale = FVector2D(FontScale, FontScale);
		HeaderItem.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(HeaderItem);
	}

	// Separator line
	TextY += HeaderHeight;
	DrawRect(BorderColor, PanelX + PanelPadding, TextY - 2.0f, PanelWidth - PanelPadding * 2.0f, 1.0f);

	// Draw entries
	for (int32 i = StartIndex; i < Entries.Num(); ++i)
	{
		const FSeinCommandLogEntry& Entry = Entries[i];

		const FString Line = FString::Printf(TEXT("[T%04d] P%d E%03d  %s"),
			Entry.Tick, Entry.PlayerIndex, Entry.EntityIndex, *Entry.Description);

		FLinearColor LineColor = FLinearColor(
			Entry.DisplayColor.R / 255.0f,
			Entry.DisplayColor.G / 255.0f,
			Entry.DisplayColor.B / 255.0f,
			1.0f);

		FCanvasTextItem TextItem(FVector2D(TextX, TextY), FText::FromString(Line), Font, LineColor);
		TextItem.Scale = FVector2D(FontScale, FontScale);
		TextItem.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(TextItem);

		TextY += LineHeight;
	}
}

ASeinPlayerController* ASeinHUD::GetSeinPlayerController() const
{
	return Cast<ASeinPlayerController>(GetOwningPlayerController());
}
