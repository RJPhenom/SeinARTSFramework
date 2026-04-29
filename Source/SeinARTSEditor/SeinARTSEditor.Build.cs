using UnrealBuildTool;

public class SeinARTSEditor : ModuleRules
{
    public SeinARTSEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "Slate",
            "SlateCore",
            "InputCore",
            "UnrealEd",
            "AssetTools",
            "ClassViewer",
            "Kismet",
            "KismetCompiler",
            "GraphEditor",
            "BlueprintGraph",
            "EditorStyle",
            "Projects",
            "PropertyEditor",
            "StructUtilsEditor",
            "StructViewer",
            "RenderCore",
            "ImageCore",
            "UMG",
            "UMGEditor",
            "AssetDefinition",
            "GameplayTags",
            "SeinARTSCore",
            "SeinARTSCoreEntity",
            "SeinARTSUIToolkit"
            // SeinARTSNavigation + SeinARTSFogOfWar are NOT deps here.
            // Each system owns its own volume details panel registration via
            // `#if WITH_EDITOR` blocks in its own module — keeps the framework
            // editor decoupled so swapping the nav or fog impl doesn't require
            // touching this module.
        });
    }
}
