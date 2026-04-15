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
            "UnrealEd",
            "AssetTools",
            "ClassViewer",
            "Kismet",
            "KismetCompiler",
            "GraphEditor",
            "BlueprintGraph",
            "EditorStyle",
            "Projects",
            "RenderCore",
            "ImageCore",
            "SeinARTSCore",
            "SeinARTSCoreEntity"
        });
    }
}
