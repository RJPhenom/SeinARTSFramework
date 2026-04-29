using UnrealBuildTool;

public class SeinARTSFogOfWar : ModuleRules
{
    public SeinARTSFogOfWar(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine",
            "SeinARTSCore", "SeinARTSCoreEntity",
            "GameplayTags",
            "RenderCore", "RHI"
        });

        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new string[] {
                "Slate", "SlateCore",
                "UnrealEd", "AssetRegistry",
                "LevelEditor",
                "PropertyEditor" // Volume details panel + bake button
            });
        }
    }
}
