// 에디터 빌드 타깃.
using UnrealBuildTool;

public class ModularFighterEditorTarget : TargetRules
{
    public ModularFighterEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.AddRange(new[] { "FighterGame", "FighterSimulation" });
    }
}
