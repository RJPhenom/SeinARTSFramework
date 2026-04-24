/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSFogOfWarModule.cpp
 * @brief   Module startup + debug toggle.
 *
 *          Registers `ShowFlags.FogOfWar` (custom show flag — UE doesn't
 *          ship one out of the box) and the `SeinARTS.Debug.ShowFogOfWar`
 *          console command that toggles it across all viewports — same UX
 *          as nav's `SeinARTS.Debug.ShowNavigation` against the built-in
 *          `ShowFlags.Navigation`. Menu-item icon resolves via
 *          `ShowFlagsMenu.FogOfWar` in the SeinARTSEditor style set
 *          (SeinGrayIcon16.svg).
 *
 *          The show flag gates rendering of the fog-of-war debug viz
 *          (scene-proxy backed, lands in the next pass). Non-PIE: whole
 *          grid red. PIE: local player controller's visible + explored cells.
 *
 *          Shipping strip: the custom show flag, console command, and all
 *          helper code are gated on `UE_ENABLE_DEBUG_DRAWING`. In
 *          `UE_BUILD_SHIPPING` StartupModule / ShutdownModule are no-ops.
 */

#include "SeinARTSFogOfWarModule.h"

#if UE_ENABLE_DEBUG_DRAWING
#include "ShowFlags.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UObject/UObjectIterator.h"
#include "Debug/SeinFogOfWarDebugComponent.h"

#if WITH_EDITOR
#include "LevelEditorViewport.h"
#include "Editor.h"
#endif
#endif // UE_ENABLE_DEBUG_DRAWING

IMPLEMENT_MODULE(FSeinARTSFogOfWarModule, SeinARTSFogOfWar)

#if UE_ENABLE_DEBUG_DRAWING

// Custom show flag registration. TCustomShowFlag ctor auto-calls
// FEngineShowFlags::RegisterCustomShowFlag on static init (module-load time,
// which is game-thread for Runtime modules). The show-flag menu queries
// registered custom flags when opened; this is all that's needed for "Fog Of
// War" to appear under the Developer group with the registered icon.
//
// Named namespace so the debug viz component (added in the next pass) can
// query the same flag via FEngineShowFlags::FindIndexByName(TEXT("FogOfWar"))
// without needing a link-time reference to this translation unit.
namespace UE::SeinARTSFogOfWar
{
	// SFG_Normal puts the entry in the top-level "Common Show Flags" section
	// alongside Navigation, Collision, etc. — matches nav's UX and where
	// RTS devs will look. SFG_Developer buries it two submenus deep under
	// All Show Flags → Developer, which is where ZoneGraph-style plugins
	// put theirs but isn't the right fit for a framework feature.
	static TCustomShowFlag<> ShowFogOfWar(
		TEXT("FogOfWar"),
		/*DefaultEnabled*/ false,
		SFG_Normal,
		NSLOCTEXT("SeinARTSFogOfWar", "ShowFogOfWar", "Fog Of War"));
}

namespace
{
	IConsoleCommand* GShowFogOfWarCmd = nullptr;
	IConsoleCommand* GPlayerPerspectiveCmd = nullptr;

	// Debug observer override. -1 = no override (use local PC). 0..255 = pinned
	// FSeinPlayerID::Value. Lockstep sim is symmetric so every client's
	// USeinFogOfWarDefault holds every player's VisionGroup; this toggle just
	// picks which one the proxy renders on THIS client.
	int32 GDebugObserverOverride = -1;

	/** Set ShowFlags.FogOfWar across all editor + game viewport clients. */
	static void SetFogOfWarShowFlag(bool bEnable)
	{
#if WITH_EDITOR
		if (GEditor)
		{
			for (FLevelEditorViewportClient* Vp : GEditor->GetLevelViewportClients())
			{
				if (Vp)
				{
					UE::SeinARTSFogOfWar::ShowFogOfWar.SetEnabled(Vp->EngineShowFlags, bEnable);
					Vp->Invalidate();
				}
			}
		}
#endif
		if (GEngine && GEngine->GameViewport)
		{
			UE::SeinARTSFogOfWar::ShowFogOfWar.SetEnabled(GEngine->GameViewport->EngineShowFlags, bEnable);
		}
	}

	/** True if any viewport currently has the flag on. */
	static bool IsFogOfWarShowFlagOn()
	{
#if WITH_EDITOR
		if (GEditor)
		{
			for (FLevelEditorViewportClient* Vp : GEditor->GetLevelViewportClients())
			{
				if (Vp && UE::SeinARTSFogOfWar::ShowFogOfWar.IsEnabled(Vp->EngineShowFlags))
				{
					return true;
				}
			}
		}
#endif
		if (GEngine && GEngine->GameViewport &&
			UE::SeinARTSFogOfWar::ShowFogOfWar.IsEnabled(GEngine->GameViewport->EngineShowFlags))
		{
			return true;
		}
		return false;
	}

	/** Force the fog debug proxies to rebuild so the observer change lands
	 *  immediately instead of on the next stamp tick. */
	static void MarkAllFogDebugProxiesDirty()
	{
		for (TObjectIterator<USeinFogOfWarDebugComponent> It; It; ++It)
		{
			if (IsValid(*It))
			{
				It->MarkRenderStateDirty();
			}
		}
	}

	static void OnPlayerPerspectiveCommand(const TArray<FString>& Args, UWorld* /*WorldContext*/)
	{
		if (Args.Num() == 0)
		{
			// Reset to local PC.
			GDebugObserverOverride = -1;
			UE_LOG(LogTemp, Log, TEXT("SeinARTS.Debug.ShowFogOfWar.PlayerPerspective = LOCAL (reset)"));
			MarkAllFogDebugProxiesDirty();
			return;
		}

		const int32 Parsed = FCString::Atoi(*Args[0]);
		if (Parsed < 0 || Parsed > 255)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("SeinARTS.Debug.ShowFogOfWar.PlayerPerspective: expected 0-255, got %s"),
				*Args[0]);
			return;
		}

		GDebugObserverOverride = Parsed;
		UE_LOG(LogTemp, Log,
			TEXT("SeinARTS.Debug.ShowFogOfWar.PlayerPerspective = Player(%d). "
				 "Use no-args to reset to local PC."),
			Parsed);
		MarkAllFogDebugProxiesDirty();
	}

	static void OnShowFogOfWarCommand(const TArray<FString>& Args, UWorld* /*WorldContext*/)
	{
		bool bEnable = !IsFogOfWarShowFlagOn();
		if (Args.Num() > 0)
		{
			const FString& A = Args[0];
			if (A == TEXT("0") || A.Equals(TEXT("off"),  ESearchCase::IgnoreCase) || A.Equals(TEXT("false"), ESearchCase::IgnoreCase))
			{
				bEnable = false;
			}
			else if (A == TEXT("1") || A.Equals(TEXT("on"), ESearchCase::IgnoreCase) || A.Equals(TEXT("true"), ESearchCase::IgnoreCase))
			{
				bEnable = true;
			}
		}
		SetFogOfWarShowFlag(bEnable);
		UE_LOG(LogTemp, Log, TEXT("SeinARTS.Debug.ShowFogOfWar = %s (ShowFlags.FogOfWar)"),
			bEnable ? TEXT("ON") : TEXT("OFF"));
	}
}

#endif // UE_ENABLE_DEBUG_DRAWING

void FSeinARTSFogOfWarModule::StartupModule()
{
#if UE_ENABLE_DEBUG_DRAWING
	if (!GShowFogOfWarCmd)
	{
		GShowFogOfWarCmd = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("SeinARTS.Debug.ShowFogOfWar"),
			TEXT("Toggle ShowFlags.FogOfWar across all viewports (same UX as SeinARTS.Debug.ShowNavigation). Drives the fog-of-war debug viz: non-PIE = whole grid red, PIE = local player controller's visible + explored cells. Usage: SeinARTS.Debug.ShowFogOfWar [0|1|on|off]."),
			FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&OnShowFogOfWarCommand),
			ECVF_Default);
	}
	if (!GPlayerPerspectiveCmd)
	{
		GPlayerPerspectiveCmd = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("SeinARTS.Debug.ShowFogOfWar.PlayerPerspective"),
			TEXT("Override the fog-of-war debug viewer to render a specific player's vision instead of the local player controller's. The sim is lockstep-symmetric — every client has every player's VisionGroup, so this is a local viewer toggle over shared data. Usage: SeinARTS.Debug.ShowFogOfWar.PlayerPerspective <id 0-255>. No args → reset to local PC. id=0 = neutral (shows only what neutral-owned sources see)."),
			FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&OnPlayerPerspectiveCommand),
			ECVF_Default);
	}
#endif
}

void FSeinARTSFogOfWarModule::ShutdownModule()
{
#if UE_ENABLE_DEBUG_DRAWING
	if (GShowFogOfWarCmd)
	{
		IConsoleManager::Get().UnregisterConsoleObject(GShowFogOfWarCmd);
		GShowFogOfWarCmd = nullptr;
	}
	if (GPlayerPerspectiveCmd)
	{
		IConsoleManager::Get().UnregisterConsoleObject(GPlayerPerspectiveCmd);
		GPlayerPerspectiveCmd = nullptr;
	}
#endif
}

namespace UE::SeinARTSFogOfWar
{
	bool TryGetDebugObserverOverride(FSeinPlayerID& OutObserver)
	{
#if UE_ENABLE_DEBUG_DRAWING
		if (GDebugObserverOverride >= 0 && GDebugObserverOverride <= 255)
		{
			OutObserver = FSeinPlayerID(static_cast<uint8>(GDebugObserverOverride));
			return true;
		}
#endif
		return false;
	}
}
