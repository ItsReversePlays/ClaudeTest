using UnrealBuildTool;

public class ClaudeTestEditorTests : ModuleRules
{
    public ClaudeTestEditorTests(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[] { "UnrealEd", "Projects", "Json", "JsonUtilities" });

        OptimizeCode = CodeOptimization.Never;
    }
}