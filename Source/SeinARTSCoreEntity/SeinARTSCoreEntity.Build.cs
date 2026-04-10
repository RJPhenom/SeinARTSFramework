using UnrealBuildTool;

public class SeinARTSCoreEntity: ModuleRules
{
    public SeinARTSCoreEntity(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateDependencyModuleNames.AddRange(new string[] {"Core", "CoreUObject", "Engine", "SeinARTSCore", "DeveloperSettings", "GameplayTags"});
    }
}
