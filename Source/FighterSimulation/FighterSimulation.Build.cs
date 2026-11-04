// 전투 코어를 언리얼에 연결하는 모듈 빌드 설정.
using UnrealBuildTool;
using System.IO;

public class FighterSimulation : ModuleRules
{
    public FighterSimulation(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.NoPCHs;
        bUseUnity = false;
        bEnableExceptions = true;
        PrivateDependencyModuleNames.Add("Core");
        PublicDefinitions.Add("MF_CORE_API=FIGHTERSIMULATION_API");
        PublicDefinitions.Add("MF_INPUT_API=FIGHTERSIMULATION_API");
        PublicDefinitions.Add("MF_NET_API=FIGHTERSIMULATION_API");
        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "../CombatCore/include"));
        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "../InputCore/include"));
        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "../NetProtocol/include"));
        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "../WindowsInput/include"));


        string FullRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../LocalData/Generated"));
        if (File.Exists(Path.Combine(FullRoot, "mf_full/modules.inc")) && File.Exists(Path.Combine(FullRoot, "mf_full/hit_geometry_data.inc")))
        {
            PublicDefinitions.Add("MF_FULL_MOVESET=1");
            PublicIncludePaths.Add(FullRoot);
            PublicIncludePaths.Add(Path.GetFullPath(Path.Combine(ModuleDirectory, "../CombatCore/src")));
        }
    }
}
