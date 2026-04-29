/**
 * SeinARTS Framework
 * Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		PluginSettings.cpp
 * @date:		1/17/2026
 * @author:		RJ Macklem
 * @brief:		Implementation of global plugin settings.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#include "Settings/PluginSettings.h"
#include "Brokers/SeinDefaultCommandBrokerResolver.h"

USeinARTSCoreSettings::USeinARTSCoreSettings()
	: SimulationTickRate(30)
	, MaxTicksPerFrame(5)
	// ResourceCatalog defaults-constructs via TArray's ctor.
	, DefaultBrokerResolverClass(USeinDefaultCommandBrokerResolver::StaticClass())
	, EffectCountWarningThreshold(256)
	// NavigationClass defaults to the shipped A* reference. Hard-coded as a
	// soft-class path string (not a StaticClass() call) because this module
	// (SeinARTSCoreEntity) intentionally does NOT depend on SeinARTSNavigation —
	// the decoupling is the whole point of the pluggable nav architecture.
	, NavigationClass(FSoftClassPath(TEXT("/Script/SeinARTSNavigation.SeinNavigationDefaultAStar")))
	, DefaultCellSize(FFixedPoint::FromInt(100))
	, DefaultMaxStepHeight(FFixedPoint::FromInt(50))
	// Network defaults — see PluginSettings.h for rationale on each. Soft path
	// for the relay class follows the established nav/fog decoupling: this
	// module deliberately does NOT depend on SeinARTSNet. Initializer order
	// here MUST match the field-declaration order in the header (Network is
	// declared between Navigation and Vision).
	, bNetworkingEnabled(true)
	, TurnRate(10)
	, InputDelayTurns(3)
	, MaxPlayers(8)
	, RelayActorClass(FSoftClassPath(TEXT("/Script/SeinARTSNet.SeinNetRelay")))
	, bDeterminismChecksEnabled(true)
	, DeterminismCheckIntervalTurns(10)
	// Drop-in/drop-out: BasicAI policy + 30s grace period default. Ships
	// `USeinNullAIController` as the framework no-op fallback so the
	// auto-spawn path is exercised end-to-end even before designers wire
	// their own AI subclass — same "minimal reference impl" pattern as the
	// shipped A* nav and default fog. Soft-class-path string because this
	// module deliberately can't reach into project code.
	, SlotDropPolicy(ESeinSlotDropPolicy::BasicAI)
	, DefaultAIControllerClass(FSoftClassPath(TEXT("/Script/SeinARTSCoreEntity.SeinNullAIController")))
	, DroppedToAITakeoverSeconds(30.0)
	, DebugFixedSessionSeed(0)
	, FogOfWarClass(FSoftClassPath(TEXT("/Script/SeinARTSFogOfWar.SeinFogOfWarDefault")))
	, VisionCellSize(FFixedPoint::FromInt(100))
	, VisionTickInterval(3)
	, FogRenderTickRate(10.0f)
#if WITH_EDITORONLY_DATA
	, bShowAbilityInBasicCategory(true)
	, bShowComponentInBasicCategory(false)
	, bShowEffectInBasicCategory(true)
	, bShowEntityInBasicCategory(true)
	, bShowWidgetInBasicCategory(false)
#endif
{
	// Vision layers: exactly 6 designer-configurable slots (N0..N5). All start
	// disabled + unnamed — the framework-default "Normal" layer is reserved as
	// the V bit and is always present without needing a slot here. Designers
	// opt in by naming + enabling slots for game-specific channels (Stealth,
	// Thermal, etc.). EditFixedSize prevents add/remove — rename or toggle only.
	// Note: we SetNum + seed here, but config load after the ctor can stomp
	// this — PostInitProperties reconciles.
	VisionLayers.SetNum(6);

	// Nav layers: 7 slots (bits 1..7). Default is reserved as bit 0. Same
	// stability + opt-in story as vision layers.
	NavLayers.SetNum(7);
}

namespace
{
	// Canonical per-slot debug colors. Authored as sRGB hex and converted
	// via `FLinearColor(FColor)` so the picker in settings shows the exact
	// hex the user intended (hex is sRGB by convention; UE's display pipe
	// gamma-corrects linear → sRGB when rendering, so going hex → FColor
	// → FLinearColor preserves the visual intent).
	//  Slot 0 (N0) 0000FF · Slot 1 (N1) 9100FF · Slot 2 (N2) E700D6
	//  Slot 3 (N3) FF5A72 · Slot 4 (N4) D4FF83 · Slot 5 (N5) 00FFA1
	static const FLinearColor DefaultLayerColors[6] = {
		FLinearColor(FColor::FromHex(TEXT("0000FF"))),
		FLinearColor(FColor::FromHex(TEXT("9100FF"))),
		FLinearColor(FColor::FromHex(TEXT("E700D6"))),
		FLinearColor(FColor::FromHex(TEXT("FF5A72"))),
		FLinearColor(FColor::FromHex(TEXT("D4FF83"))),
		FLinearColor(FColor::FromHex(TEXT("00FFA1"))),
	};

	// Per-slot canonical nav-layer colors. Warm / pink-red / earth-tone
	// spectrum — intentionally avoids greens and cyans (those clash with the
	// vision-layer palette and the static nav cell green/red). Each slot is
	// distinct enough at-a-glance for layered debug viz to read clearly.
	//  Slot 0 (N0) FFA500 · Slot 1 (N1) FFD600 · Slot 2 (N2) FA8072
	//  Slot 3 (N3) DDA0DD · Slot 4 (N4) FF6347 · Slot 5 (N5) C71585
	//  Slot 6 (N6) 8B4513
	static const FLinearColor DefaultNavLayerColors[7] = {
		FLinearColor(FColor::FromHex(TEXT("FFA500"))),
		FLinearColor(FColor::FromHex(TEXT("FFD600"))),
		FLinearColor(FColor::FromHex(TEXT("FA8072"))),
		FLinearColor(FColor::FromHex(TEXT("DDA0DD"))),
		FLinearColor(FColor::FromHex(TEXT("FF6347"))),
		FLinearColor(FColor::FromHex(TEXT("C71585"))),
		FLinearColor(FColor::FromHex(TEXT("8B4513"))),
	};
}

void USeinARTSCoreSettings::PostInitProperties()
{
	Super::PostInitProperties();

	// Config load (DefaultEngine.ini) runs AFTER the ctor. An older INI can
	// shrink the array or leave DebugColor at the struct default (white).
	// Force exactly 6 slots and re-seed any color that's still plain white
	// with the per-slot canonical value — idempotent on fresh projects,
	// corrective on legacy ones.
	if (VisionLayers.Num() != 6)
	{
		VisionLayers.SetNum(6);
	}
	for (int32 i = 0; i < 6; ++i)
	{
		if (VisionLayers[i].DebugColor == FLinearColor::White)
		{
			VisionLayers[i].DebugColor = DefaultLayerColors[i];
		}
	}

	// Same reconcile pattern for nav layers — enforce slot count, re-seed
	// any white DebugColor to the canonical per-slot value.
	if (NavLayers.Num() != 7)
	{
		NavLayers.SetNum(7);
	}
	for (int32 i = 0; i < 7; ++i)
	{
		if (NavLayers[i].DebugColor == FLinearColor::White)
		{
			NavLayers[i].DebugColor = DefaultNavLayerColors[i];
		}
	}
}

FName USeinARTSCoreSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

#if WITH_EDITOR
FText USeinARTSCoreSettings::GetSectionText() const
{
	return NSLOCTEXT("SeinARTSCore", "SeinARTSCoreSettingsSection", "SeinARTS");
}

FText USeinARTSCoreSettings::GetSectionDescription() const
{
	return NSLOCTEXT("SeinARTSCore", "SeinARTSCoreSettingsDescription",
		"Configure SeinARTS simulation, editor, and content creation settings.");
}
#endif
