// 게임 빌드 타깃.
using UnrealBuildTool;

public class ModularFighterTarget : TargetRules
{
    public ModularFighterTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.AddRange(new[] { "FighterGame", "FighterSimulation" });
    }
}
