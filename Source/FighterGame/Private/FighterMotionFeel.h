// 포즈 전환과 카메라 추적의 표시 옵션.
#pragma once
#include "CoreMinimal.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/IConsoleManager.h"

namespace FighterMotionFeel
{
inline bool HardCut()
{
    static const bool Value = FParse::Param(FCommandLine::Get(), TEXT("FighterHardCut")) ||
                              !FParse::Param(FCommandLine::Get(), TEXT("FighterNoHardCut"));

    return Value;
}

inline bool Lock60()
{
    static const bool Value = FParse::Param(FCommandLine::Get(), TEXT("FighterLock60")) ||
                              !FParse::Param(FCommandLine::Get(), TEXT("FighterNoLock60"));

    return Value;
}

inline bool FrameTrace()
{
    static const bool Value = FParse::Param(FCommandLine::Get(), TEXT("FighterFrameTrace"));

    return Value;
}

inline void ApplyFrameOptions()
{
    static bool Applied = false;

    if (Applied)
        return;
    Applied = true;
    auto Set = [](const TCHAR* Name, int32 Value)
    {
        if (auto* C = IConsoleManager::Get().FindConsoleVariable(Name))
            C->Set(Value, ECVF_SetByCommandline);
    };
    const bool Lock = Lock60();

    if (Lock)
    {
        Set(TEXT("t.MaxFPS"), 60);
        Set(TEXT("r.VSync"), 1);
    }

    FString AA;

    if (FParse::Value(FCommandLine::Get(), TEXT("FighterAA="), AA))
    {
        int32 Method = -1;

        if (AA.Equals(TEXT("None"), ESearchCase::IgnoreCase))
            Method = 0;
        else if (AA.Equals(TEXT("TAA"), ESearchCase::IgnoreCase))
            Method = 2;
        else if (AA.Equals(TEXT("TSR"), ESearchCase::IgnoreCase))
            Method = 4;

        if (Method >= 0)
            Set(TEXT("r.AntiAliasingMethod"), Method);
        else
            UE_LOG(LogTemp, Warning, TEXT("Invalid FighterAA=%s; expected None/TAA/TSR"), *AA);
    }

    if (HardCut())
        Set(TEXT("r.MotionBlurQuality"), 0);

    const bool bHighQuality = FParse::Param(FCommandLine::Get(), TEXT("FighterHighQuality"));
    int32 ScreenPercentage = bHighQuality ? 150 : 100;
    FParse::Value(FCommandLine::Get(), TEXT("FighterScreenPercentage="), ScreenPercentage);
    Set(TEXT("r.ScreenPercentage"), FMath::Clamp(ScreenPercentage, 25, 200));

    if (!bHighQuality)
    {
        Set(TEXT("sg.GlobalIlluminationQuality"), 2);
        Set(TEXT("sg.ReflectionQuality"), 2);
        Set(TEXT("sg.ShadowQuality"), 2);
    }

    Set(TEXT("r.DynamicRes.OperationMode"), 0);
    Set(TEXT("r.MaxAnisotropy"), 16);
    Set(TEXT("r.Streaming.PoolSize"), 4096);
    Set(TEXT("r.Streaming.LimitPoolSizeToVRAM"), 1);
    Set(TEXT("r.Streaming.MipBias"), 0);
    Set(TEXT("r.TSR.History.R11G11B10"), 0);
    Set(TEXT("r.TSR.History.ScreenPercentage"), bHighQuality ? 200 : 100);
    Set(TEXT("r.TSR.History.UpdateQuality"), bHighQuality ? 3 : 2);
    Set(TEXT("r.TSR.RejectionAntiAliasingQuality"), 2);
    Set(TEXT("r.TSR.ShadingRejection.Flickering"), 1);
    Set(TEXT("r.SSS.HalfRes"), 0);
    Set(TEXT("r.SSS.Quality"), 1);
    Set(TEXT("r.SSS.SampleSet"), 2);
    Set(TEXT("r.Lumen.Reflections.DownsampleFactor"), bHighQuality ? 1 : 2);
    Set(TEXT("r.SSR.Quality"), 4);
    Set(TEXT("r.SSR.HalfResSceneColor"), 0);
    auto Get = [](const TCHAR* Name)
    {
        const auto* C = IConsoleManager::Get().FindConsoleVariable(Name);

        return C ? C->GetFloat() : -1.f;
    };
    UE_LOG(
        LogTemp, Display,
        TEXT(
            "FIGHTER_FRAME_PROFILE hard_cut=%d lock60=%d max_fps=%g vsync=%g aa=%g motion_blur_quality=%g screen_percentage=%g fixed_step=%d default_hard_cut=1 default_lock60=1"),
        HardCut(), Lock, Get(TEXT("t.MaxFPS")), Get(TEXT("r.VSync")), Get(TEXT("r.AntiAliasingMethod")),
        Get(TEXT("r.MotionBlurQuality")), Get(TEXT("r.ScreenPercentage")),
        FParse::Param(FCommandLine::Get(), TEXT("UseFixedTimeStep")));
    UE_LOG(
        LogTemp, Display,
        TEXT(
            "FIGHTER_HIGH_QUALITY screen_percentage=%g anisotropy=%g texture_pool_mb=%g sss_half_res=%g tsr_history_percentage=%g reflections_downsample=%g"),
        Get(TEXT("r.ScreenPercentage")), Get(TEXT("r.MaxAnisotropy")), Get(TEXT("r.Streaming.PoolSize")),
        Get(TEXT("r.SSS.HalfRes")), Get(TEXT("r.TSR.History.ScreenPercentage")),
        Get(TEXT("r.Lumen.Reflections.DownsampleFactor")));
}

inline int32 Mask()
{
    static const int32 Value = []()
    {
        int32 Result = 7;
        FParse::Value(FCommandLine::Get(), TEXT("FighterMotionFeelMask="), Result);

        if (FParse::Param(FCommandLine::Get(), TEXT("FighterLegacyMotionFeel")))
            Result = 0;

        return FMath::Clamp(Result, 0, 7);
    }();

    return Value;
}

inline bool Trace()
{
    static const bool Value = FParse::Param(FCommandLine::Get(), TEXT("FighterMotionFeelTrace"));

    return Value;
}

inline bool Pose()
{
    return (Mask() & 1) != 0;
}

inline float AxisLag()
{
    return HardCut() ? 0.f : (Mask() & 2) ? 0.15f : 0.3f;
}

inline float BlurAmount()
{
    return HardCut() ? 0.f : (Mask() & 4) ? 0.1f : 0.25f;
}

inline float BlurMax()
{
    return HardCut() ? 0.f : (Mask() & 4) ? 1.f : 2.f;
}
}
