// 언리얼 표시 모듈 빌드 설정.
using UnrealBuildTool;

public class FighterGame : ModuleRules
{
    public FighterGame(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false;
        bEnableExceptions = true;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "FighterSimulation", "GameplayAbilities", "GameplayTags", "GameplayTasks", "ModularGameplay", "GameFeatures", "EnhancedInput" });
        PrivateDependencyModuleNames.AddRange(new[] { "Json", "JsonUtilities", "RHI", "RenderCore", "Niagara", "NiagaraCore", "Sockets", "Networking", "Slate", "SlateCore", "MediaAssets", "AudioMixer", "ProceduralMeshComponent" });
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.Add("UnrealEd");
        }
    }
}
