#include "SeinARTSNavigationModule.h"

#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "ShowFlags.h"
#include "Misc/EngineVersionComparison.h"

#if WITH_EDITOR
#include "LevelEditor.h"
#include "LevelEditorViewport.h"
#endif

IMPLEMENT_MODULE(FSeinARTSNavigationModule, SeinARTSNavigation)

namespace
{
	IConsoleCommand* GNavDebugToggleCommand = nullptr;

	/** Flip the Navigation show flag across all applicable viewports (editor + PIE/game).
	 *  Each viewport's FSceneView exposes its own EngineShowFlags; USeinNavDebugRenderingComponent's
	 *  scene proxy consults them in GetViewRelevance, so per-viewport draw state stays accurate. */
	static void ToggleNavigationShowFlag(bool bEnable)
	{
#if WITH_EDITOR
		if (GEditor)
		{
			for (FLevelEditorViewportClient* Vp : GEditor->GetLevelViewportClients())
			{
				if (Vp)
				{
					Vp->EngineShowFlags.SetNavigation(bEnable);
					Vp->Invalidate();
				}
			}
		}
#endif
		// Game viewport (PIE).
		if (GEngine && GEngine->GameViewport)
		{
			GEngine->GameViewport->EngineShowFlags.SetNavigation(bEnable);
		}
	}

	static bool IsNavigationShowFlagOn()
	{
#if WITH_EDITOR
		if (GEditor)
		{
			for (const FLevelEditorViewportClient* Vp : GEditor->GetLevelViewportClients())
			{
				if (Vp && Vp->EngineShowFlags.Navigation)
				{
					return true;
				}
			}
		}
#endif
		if (GEngine && GEngine->GameViewport && GEngine->GameViewport->EngineShowFlags.Navigation)
		{
			return true;
		}
		return false;
	}

	static void OnNavDebugCommand(const TArray<FString>& Args, UWorld* /*WorldContext*/)
	{
		bool bEnable = !IsNavigationShowFlagOn();
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
		ToggleNavigationShowFlag(bEnable);
		UE_LOG(LogTemp, Log, TEXT("SeinARTS.Navigation.Debug = %s (ShowFlags.Navigation)"), bEnable ? TEXT("ON") : TEXT("OFF"));
	}
}

void FSeinARTSNavigationModule::StartupModule()
{
	if (!GNavDebugToggleCommand)
	{
		GNavDebugToggleCommand = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("SeinARTS.Navigation.Debug"),
			TEXT("Toggle the ShowFlags.Navigation viewport flag — makes every Sein nav-debug scene proxy render (green = walkable, red = blocked). Usage: SeinARTS.Navigation.Debug [0|1|on|off]. Equivalent to `showflag.navigation` + UE's 'P' key."),
			FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&OnNavDebugCommand),
			ECVF_Default);
	}
}

void FSeinARTSNavigationModule::ShutdownModule()
{
	if (GNavDebugToggleCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(GNavDebugToggleCommand);
		GNavDebugToggleCommand = nullptr;
	}
}
