/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWorldSettings.h
 * @brief   Per-level WorldSettings carrying the level-scoped match-settings
 *          fallback used during PIE and any other lobby-less load path.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/WorldSettings.h"
#include "Data/SeinMatchSettings.h"
#include "SeinWorldSettings.generated.h"

/**
 * SeinARTS per-level WorldSettings.
 *
 * Carries the level-scoped match-settings fallback used during PIE testing
 * (and any other path that loads the level without a lobby providing match
 * data). Game mode resolution chain is:
 *   1. Runtime override from UGameInstance (set by lobby before OpenLevel)
 *   2. Per-level: this WorldSettings (preset > inline)
 *   3. Plugin settings: project-wide PIE default preset
 *   4. Hardcoded fallback (single Human slot)
 *
 * To enable: Project Settings → Maps & Modes → World Settings Class →
 * `SeinWorldSettings`. Per-level edits via Window → World Settings.
 *
 * Match-settings storage is preset-then-inline: a `USeinMatchSettingsPreset`
 * reference for reuse across maps, with an inline `FSeinMatchSettings`
 * fallback for one-off / scenario-specific levels.
 */
UCLASS(Blueprintable, ClassGroup = (SeinARTS), meta = (DisplayName = "Sein World Settings"))
class SEINARTSFRAMEWORK_API ASeinWorldSettings : public AWorldSettings
{
	GENERATED_BODY()

public:
	/** Reusable preset asset. Checked first; if null, `InlineMatchSettings` wins. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Match")
	TObjectPtr<USeinMatchSettingsPreset> MatchSettingsPreset;

	/** Inline fallback for one-off levels that don't warrant a preset asset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Match",
		meta = (EditCondition = "MatchSettingsPreset == nullptr"))
	FSeinMatchSettings InlineMatchSettings;

	/** Resolved per-level match settings (preset > inline). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Match", meta = (DisplayName = "Get Effective Match Settings"))
	const FSeinMatchSettings& GetEffectiveMatchSettings() const;
};
