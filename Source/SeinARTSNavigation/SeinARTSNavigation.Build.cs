using UnrealBuildTool;

public class SeinARTSNavigation : ModuleRules
{
    public SeinARTSNavigation(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine",
            "SeinARTSCore", "SeinARTSCoreEntity",
            "GameplayTags",
            "PhysicsCore",
            "RenderCore", "RHI"
        });

        // Editor-only dependencies for the nav bake pipeline (toast notifications,
        // asset registry, package save) + debug actor's editor-viewport camera pick.
        // Stripped from shipping builds.
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new string[] {
                "Slate", "SlateCore",
                "UnrealEd", "AssetRegistry",
                "LevelEditor"
            });
        }
    }
}
