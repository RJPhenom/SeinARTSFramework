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
        });
    }
}
