using UnrealBuildTool;

public class SeinARTSNavigation : ModuleRules
{
    public SeinARTSNavigation(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine",
            "SeinARTSCore", "SeinARTSCoreEntity",
            "GameplayTags"
        });
    }
}
