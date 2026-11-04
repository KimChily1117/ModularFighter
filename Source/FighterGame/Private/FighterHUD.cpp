// 체력바, 라운드, 로비, 트레이닝 정보를 그리는 HUD.
#include "FighterHUD.h"
#include "FighterImpactComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "FighterArena.h"
#include "FighterAbilityHost.h"
#include "FighterPawnData.h"
#include "FighterTrainingComponent.h"
#include "FighterNetSession.h"
#include "FighterStageComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Engine/Texture2D.h"
#include "Engine/Font.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "CanvasItem.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/FontMeasure.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/Paths.h"
#include "Rendering/SlateRenderer.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"

void AFighterHUD::BeginPlay()
{
    Super::BeginPlay();
    KazuyaPortrait = LoadObject<UTexture2D>(
        nullptr, TEXT("/Game/LocalPresentation/UI/T_UI_HUD_Character_Icon_L_grl.T_UI_HUD_Character_Icon_L_grl"));
    PaulPortrait = LoadObject<UTexture2D>(
        nullptr, TEXT("/Game/LocalPresentation/UI/T_UI_HUD_Character_Icon_R_grf.T_UI_HUD_Character_Icon_R_grf"));
    InfiniteTimer = LoadObject<UTexture2D>(nullptr, TEXT("/Game/LocalPresentation/UI/T_UI_HUD_Timer_Infinite"));
    TimerDigits = LoadObject<UTexture2D>(nullptr, TEXT("/Game/LocalPresentation/UI/T_UI_HUD_Timer_Num"));
    TimerCountdownDigits =
        LoadObject<UTexture2D>(nullptr, TEXT("/Game/LocalPresentation/UI/T_UI_HUD_Timer_Num_Countdown"));

    for (const TCHAR* Side : {TEXT("L"), TEXT("R")})
    {
        GaugeFrames.Add(LoadObject<UTexture2D>(
            nullptr, *FString::Printf(TEXT("/Game/LocalPresentation/UI/T_UI_HUD_Gauge_Base_%s"), Side)));
        GaugeFills.Add(LoadObject<UMaterialInterface>(
            nullptr, *FString::Printf(TEXT("/Game/LocalPresentation/UI/M_SourceHP_%s"), Side)));
    }

    auto RuntimeFont = [this](const TCHAR* File) -> UFont*
    {
        const FString Path =
            FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("LocalData/Presentation/fonts") / File);

        if (!FPaths::FileExists(Path))
            return nullptr;
        UFont* Font = NewObject<UFont>(this);
        Font->FontCacheType = EFontCacheType::Runtime;
        Font->LegacyFontSize = 16;
        FTypefaceEntry& Entry = Font->GetMutableInternalCompositeFont().DefaultTypeface.Fonts.AddDefaulted_GetRef();
        Entry.Name = TEXT("Regular");
        Entry.Font = FFontData(Path, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);

        return Font;
    };
    BodyFont = RuntimeFont(TEXT("OT-ClarimoUDKRPro-Medium.otf"));
    TitleFont = RuntimeFont(TEXT("SEOLSANS-MEDIUM.otf"));
    LobbyFont = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto"));

    for (const TCHAR* Name : {TEXT("T_UI_SideSelect_L"),
                              TEXT("T_UI_SideSelect_R"),
                              TEXT("T_UI_SideSelect_L_chara"),
                              TEXT("T_UI_SideSelect_R_chara"),
                              TEXT("T_UI_SideSelect_Glow_add"),
                              TEXT("T_UI_SideSelect_Line_Light"),
                              TEXT("Panel_Normal_Base"),
                              TEXT("Panel_Selected_Base"),
                              TEXT("Panel_P1_Selected_OuterGlow"),
                              TEXT("Panel_P1_Selected_InnerGlow"),
                              TEXT("Panel_P1_Selected_Text_P1"),
                              TEXT("Panel_P2_Selected_OuterGlow"),
                              TEXT("Panel_P2_Selected_InnerGlow"),
                              TEXT("Panel_P2_Selected_Text_P2"),
                              TEXT("T_UI_LM_Plate"),
                              TEXT("T_UI_LM_Plate_Add"),
                              TEXT("T_UI_RS_Button_Hover"),
                              TEXT("T_UI_RS_Button_Hover_Add"),
                              TEXT("T_UI_RS_Eff_PanelGlow_Add"),
                              TEXT("T_UI_MainMenu_Bg_Gradation"),
                              TEXT("T_UI_MM_Banner_Cursor"),
                              TEXT("T_UI_MM_Banner_Frame"),
                              TEXT("T_UI_MM_Banner_Cursor_Gra"),
                              TEXT("T_UI_MM_Shortcut_Icon_Matching"),
                              TEXT("T_UI_MM_Shortcut_Icon_Quit"),
                              TEXT("T_UI_JB_Stage_MainMenu"),
                              TEXT("T_UI_SS_Stage_Load_st02_NeoCity"),
                              TEXT("T_UI_SS_Stage_Load_st03_Island"),
                              TEXT("T_FG_SS_Panel_st02_NeoCity"),
                              TEXT("T_FG_SS_Panel_st03_Island"),
                              TEXT("T_UI_SS_Stage_Panel_SelectBG"),
                              TEXT("T_UI_SS_Stage_Panel_SelectRim"),
                              TEXT("T_UI_SS_Stage_Panel_shadow"),
                              TEXT("T_UI_SS_Stage_BG_shadow"),
                              TEXT("T_UI_PL_Cursor"),
                              TEXT("T_UI_PL_Menu_Line")})
        LobbyTexture(Name);

    for (const TCHAR* Code : {TEXT("grl"), TEXT("grf")})
        for (const TCHAR* Kind : {TEXT("T_UI_CS_BG_Character_L_"), TEXT("T_UI_CS_BG_Character_R_"),
                                  TEXT("T_UI_CS_Character_Name_"), TEXT("T_UI_CS_BG_Character_Name_"),
                                  TEXT("T_UI_CS_Character_Thumb_Normal_"), TEXT("T_UI_CS_Character_Thumb_Selected_")})
            LobbyTexture(*(FString(Kind) + Code));
    UE_LOG(LogTemp, Display, TEXT("FIGHTER_HUD lobby fonts body=%d title=%d"), BodyFont ? 1 : 0, TitleFont ? 1 : 0);
    UE_LOG(LogTemp, Display, TEXT("FIGHTER_HUD_SOURCE frames=%d fills=%d timer=%d"),
           GaugeFrames[0] && GaugeFrames[1] ? 2 : 0, GaugeFills[0] && GaugeFills[1] ? 2 : 0, InfiniteTimer ? 1 : 0);
    UE_LOG(LogTemp, Display, TEXT("FIGHTER_HUD portraits=%d/%d max_hp=%d"), KazuyaPortrait ? 1 : 0,
           PaulPortrait ? 1 : 0, modular_fighter::InitialHp);
}

// 현재 경기 상태에 맞는 UI를 그린다.
void AFighterHUD::DrawHUD()
{
    Super::DrawHUD();

    if (!Canvas)
        return;

    if (!Arena)
        for (TActorIterator<AFighterArena> It(GetWorld()); It; ++It)
        {
            Arena = *It;
            break;
        }
    if (!Arena)
        return;
    const auto& State = Arena->GetCombatState();
    const auto& Inputs = Arena->GetFrameInputs();
    const int32 Controlled = Arena->GetControlledFighter();
    const UFighterTrainingComponent* Training = Arena->GetTraining();

    auto PawnText = [this](int32 Index, FString UFighterPawnData::*Field)
    {
        const UFighterPawnData* PawnData = Arena->GetPawnData(Index);

        return PawnData ? PawnData->*Field : FString();
    };
    const float W = Canvas->SizeX;
    const float H = Canvas->SizeY;
    const float Scale = FMath::Clamp(FMath::Min(W / 1280.f, H / 720.f), 0.35f, 2.f);
    const float Margin = 36 * Scale;

    {
        FVector Eye, Look;
        float Fov = 0.f, Progress = 0.f;

        if (Arena->GetHeatCutIn(Eye, Look, Fov, Progress))
        {
            DrawRect(FLinearColor(0.02f, 0.05f, 0.22f, 0.42f), 0, 0, W, H);
            DrawRect(FLinearColor(0.f, 0.f, 0.03f, 0.35f), 0, 0, W, H * 0.12f);
            DrawRect(FLinearColor(0.f, 0.f, 0.03f, 0.35f), 0, H * 0.88f, W, H * 0.12f);
        }
    }

    {
        using EPractice = AFighterArena::EPracticePhase;
        const EPractice Practice = Arena->GetPracticePhase();

        if (Practice == EPractice::GetReady)
        {
            DrawRoundPresentation(W, H, Scale);

            return;
        }

        if (Practice == EPractice::Opening || Practice == EPractice::Title || Practice == EPractice::Select ||
            Practice == EPractice::MainMenu || Practice == EPractice::StageSelect || Practice == EPractice::Searching ||
            (Practice == EPractice::Matched && !Arena->GetNet()->InMatch() && !Arena->GetNet()->GetResult()))
        {
            DrawLobby(W, H, Scale);
            DrawMenuFade(W, H);

            return;
        }
    }

    if (Arena->IsOnline() && (!Arena->GetNet()->InMatch() || Arena->GetNet()->GetResult()) &&
        !DrawMatchEnd(W, H, Scale))
    {
        DrawLobby(W, H, Scale);

        return;
    }

    const FLinearColor Ink(0.025f, 0.035f, 0.055f, 0.94f);
    const FLinearColor White(0.88f, 0.93f, 1.f);
    const FLinearColor Muted(0.45f, 0.58f, 0.68f);

    if (Arena->GetPracticePhase() == AFighterArena::EPracticePhase::Training)
    {
        const UFighterNetSession* Net = Arena->GetNet();
        const int32 Dots = static_cast<int32>(GetWorld()->GetTimeSeconds() * 2.0) % 4;
        const FString Line = !Arena->IsPracticeMatching() ? FString(TEXT("Esc: 메뉴  ·  M / View: 매칭 켜기"))
                             : Net->IsConnected()
                                 ? FString(TEXT("매칭 중")) + FString::ChrN(Dots, TEXT('.')) + TEXT("  (M: 끄기)")
                                 : FString(TEXT("서버에 연결할 수 없습니다  (M: 끄기)"));
        float TW = 0, TH = 0;
        UFont* Font = GEngine->GetMediumFont();
        GetTextSize(Line, TW, TH, Font, Scale);

        DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), W * 0.5f - TW * 0.5f - 10 * Scale, 156 * Scale, TW + 20 * Scale,
                 TH + 8 * Scale);
        DrawText(Line, Arena->IsPracticeMatching() ? FLinearColor(1.f, 0.82f, 0.35f) : White, W * 0.5f - TW * 0.5f,
                 160 * Scale, Font, Scale, false);
    }

    auto Label = [this, Scale](const FString& Text, float X, float Y, const FLinearColor& Color, float Size = 1.f)
    {
        DrawText(Text, Color, X, Y, GEngine->GetMediumFont(), Scale * Size, false);
    };

    const float BarW = W * .5f - 168 * Scale;
    const float Centre = W * .5f;
    const uint32 HealthTicks = State.frame >= HealthLastCoreFrame ? State.frame - HealthLastCoreFrame : 0;

    if (State.frame < HealthLastCoreFrame)
        for (int32 Index = 0; Index < 2; ++Index)
            HealthTrails[Index].Reset(State.fighters[Index].hp, modular_fighter::InitialHp);
    HealthLastCoreFrame = State.frame;

    for (int32 Index = 0; Index < 2; ++Index)
    {
        const int32 Slot = Arena->IsViewFlipped() ? 1 - Index : Index;
        const bool HealthChanged =
            HealthTrails[Index].Initialized && HealthTrails[Index].LastHp != State.fighters[Index].hp;

        if (!HealthTrails[Index].Initialized)
            HealthTrails[Index].Reset(State.fighters[Index].hp, modular_fighter::InitialHp);

        for (uint32 Tick = 0; Tick < HealthTicks; ++Tick)
            HealthTrails[Index].Tick(State.fighters[Index].hp, modular_fighter::InitialHp);

        if (HealthChanged)
            UE_LOG(LogTemp, Display,
                   TEXT("FIGHTER_T6_GAUGE side=%d hp=%d remaining=%d trail=%.6f alpha=%.6f frame=%u native_policy=1"),
                   Index, State.fighters[Index].hp, HealthTrails[Index].Remaining, HealthTrails[Index].Trail,
                   HealthTrails[Index].Alpha, State.frame);
        const auto* Feedback = Arena->GetImpactPresentation();
        const FVector2D Shake =
            Feedback ? Feedback->GetHealthBarOffset(Index) * Scale * FVector2D(Arena->GetViewAxisSign(), 1.f)
                     : FVector2D::ZeroVector;
        const float Flash = Feedback ? Feedback->GetHealthBarFlash(Index) : 0.f;
        const float BaseX = Slot == 0 ? 118 * Scale : Centre + 50 * Scale;
        const float X = BaseX + Shake.X, Y = 48 * Scale + Shake.Y;
        const float Ratio = FMath::Clamp(float(State.fighters[Index].hp) / modular_fighter::InitialHp, 0.f, 1.f);
        const float Fill = BarW * Ratio;

        const float FillX = Slot == 0 ? X + BarW - Fill : X;

        if (GaugeFrames.IsValidIndex(Slot) && GaugeFrames[Slot])
            DrawTexture(GaugeFrames[Slot], X - 2 * Scale, Y - 4 * Scale, BarW + 4 * Scale, 32 * Scale,
                        (Slot == 0 ? 444.f : 121.f) / 1920.f, 80.f / 240.f, 1355.f / 1920.f, 82.f / 240.f,
                        FLinearColor::White, BLEND_Translucent);
        else
            DrawRect(FLinearColor(.035f, .02f, .025f), X, Y, BarW, 24 * Scale);

        const float TrailFill = BarW * FMath::Clamp(HealthTrails[Index].Trail, Ratio, 1.f);

        if (TrailFill > Fill)
            DrawRect(FLinearColor(1.f, 1.f, 1.f, HealthTrails[Index].Alpha),
                     Slot == 0 ? X + BarW - TrailFill : X + Fill, Y + 2 * Scale, TrailFill - Fill, 20 * Scale);

        if (Fill > 0 && GaugeFills.IsValidIndex(Slot) && GaugeFills[Slot])
            DrawMaterial(GaugeFills[Slot], FillX, Y - 3 * Scale, Fill, 30 * Scale, Slot == 0 ? 1.f - Ratio : 0.f, 0,
                         Ratio, 1);
        else if (Fill > 0)
            DrawRect(FLinearColor(.9f, .35f, .03f), FillX, Y, Fill, 24 * Scale);

        if (modular_fighter::RageActive(State.fighters[Index]))
            DrawRect(FLinearColor(1.f, .025f, .015f, .55f), FillX, Y, Fill, 24 * Scale);

        if (Flash > 0)
            DrawRect(FLinearColor(1.f, .88f, .65f, Flash * .22f), FillX, Y, Fill, 24 * Scale);
        const float HeatW = BarW * .65f, HeatX = Slot == 0 ? BaseX + BarW - HeatW : BaseX;
        DrawRect(FLinearColor(.08f, .09f, .18f, .85f), HeatX, 78 * Scale, HeatW, 7 * Scale);
        const auto& HeatFighter = State.fighters[Index];
        const float HeatRatio = HeatFighter.heat_frames
                                    ? float(HeatFighter.heat_frames) / modular_fighter::HeatDurationFrames
                                : HeatFighter.heat_used ? 0.f
                                                        : 1.f;
        const float HeatFill = HeatW * HeatRatio;

        if (HeatFill > 0)
            DrawRect(HeatFighter.heat_frames ? FLinearColor(.38f, .6f, 1.f) : FLinearColor(.18f, .26f, .45f),
                     Slot == 0 ? HeatX + HeatW - HeatFill : HeatX, 78 * Scale, HeatFill, 7 * Scale);
        DrawLine(HeatX, 85 * Scale, HeatX + HeatW, 85 * Scale, FLinearColor(.32f, .3f, .52f), Scale);
        Label(TEXT("히트"), Slot == 0 ? HeatX : HeatX + HeatW - 30 * Scale, 88 * Scale, Muted, .48f);

        for (int32 Pip = 0; Pip < 3; ++Pip)
        {
            const float PX = Centre + (Slot == 0 ? -1 : 1) * (78 + Pip * 22) * Scale;
            const float PY = 32 * Scale, R = 5 * Scale;

            if (Arena->IsOnline() && Arena->GetNet()->GetRound())
            {
                const auto& Rd = *Arena->GetNet()->GetRound();

                if (Pip < (Index == 0 ? Rd.left_wins : Rd.right_wins))
                    DrawRect(FLinearColor(1.f, .78f, .3f, .95f), PX - R * .8f, PY - R * .8f, R * 1.6f, R * 1.6f);
            }

            for (int32 Side = 0; Side < 12; ++Side)
            {
                const float A = Side * 2 * PI / 12, B = (Side + 1) * 2 * PI / 12;
                DrawLine(PX + FMath::Cos(A) * R, PY + FMath::Sin(A) * R, PX + FMath::Cos(B) * R, PY + FMath::Sin(B) * R,
                         FLinearColor(.65f, .55f, .44f, .8f), Scale);
            }
        }

        const bool bPlaceholder = FParse::Param(FCommandLine::Get(), TEXT("FighterPlaceholderBodies"));

        const bool bPaulHere = State.fighters[Index].character == modular_fighter::CharacterId::Paul;
        UTexture2D* Portrait = bPaulHere ? PaulPortrait.Get() : KazuyaPortrait.Get();
        const bool bMirror = (Slot == 0) == bPaulHere;

        if (Portrait && !bPlaceholder)
            DrawTexture(Portrait, Slot == 0 ? 8 * Scale : W - 172 * Scale, 8 * Scale, 164 * Scale, 82 * Scale,
                        bMirror ? 1.f : 0.f, 0, bMirror ? -1.f : 1.f, 1, FLinearColor::White, BLEND_Translucent);
        Label(bPlaceholder ? FString(Index == 0 ? TEXT("P1") : TEXT("P2")) : Arena->GetCharacterLabel(Index),
              Slot == 0 ? 35 * Scale : W - 94 * Scale, 86 * Scale, White, .83f);
        Label(FString::Printf(TEXT("%d / %d"), State.fighters[Index].hp, modular_fighter::InitialHp),
              Slot == 0 ? BaseX : BaseX + BarW - 55 * Scale, 103 * Scale, White, .62f);
    }

    if (Arena->IsOnline() && Arena->GetNet()->InMatch() && TimerDigits)
    {
        const int64 FramesLeft =
            FMath::Max<int64>(0, int64(Arena->GetNet()->GetRoundFrameLimit()) - int64(State.frame));
        const int32 Seconds = FMath::Clamp<int32>(int32((FramesLeft + 59) / 60), 0, 99);
        UTexture2D* Digits = Seconds <= 10 && TimerCountdownDigits ? TimerCountdownDigits.Get() : TimerDigits.Get();

        const float DigitH = 70 * Scale, DigitW = DigitH * 160.f / 208.f, Pitch = 43 * Scale,
                    DigitY = 60 * Scale - DigitH * .5f;

        for (int32 Place = 0; Place < 2; ++Place)
            DrawTexture(Digits, Centre + (Place - .5f) * Pitch - DigitW * .5f, DigitY, DigitW, DigitH,
                        (Place == 0 ? Seconds / 10 : Seconds % 10) / 10.f, 0, .1f, 1, FLinearColor::White,
                        BLEND_Translucent);
    }
    else if (InfiniteTimer)
        DrawTexture(InfiniteTimer, Centre - 43 * Scale, 27 * Scale, 86 * Scale, 52 * Scale, 15.f / 272.f, 31.f / 208.f,
                    242.f / 272.f, 146.f / 208.f, FLinearColor::White, BLEND_Translucent);
    Label(Arena->IsOnline() ? TEXT("온라인") : TEXT("트레이닝"), Centre - 24 * Scale,
          (Arena->IsOnline() ? 100 : 90) * Scale, Muted, .48f);

    if (Training && !Arena->IsReferencePreview() && !Arena->IsOnline())
    {
        const TCHAR* RageMode = Training->GetRageMode() == 1   ? TEXT("켜기")
                                : Training->GetRageMode() == 2 ? TEXT("끄기")
                                                               : TEXT("자동");
        Label(FString::Printf(TEXT("F9 레이지: %s"), RageMode), Centre - 42 * Scale, 108 * Scale, Muted, .45f);
    }

    if (Training && !Arena->IsReferencePreview())
        for (int32 Index = 0; Index < 2; ++Index)
        {
            const auto& Combo = Training->GetComboDisplay(Index);

            if (Combo.Hits == 0)
                continue;
            const float X = (Arena->IsViewFlipped() ? 1 - Index : Index) == 0 ? Margin : W - Margin - BarW;
            const bool bActive = State.fighters[1 - Index].combo_hits > 0;
            Label(FString::Printf(TEXT("%d히트   %d 대미지   /   마지막 %d%s"), Combo.Hits, Combo.Damage,
                                  Combo.LastDamage, bActive ? TEXT("") : TEXT("   (지난 콤보)")),
                  X, 242 * Scale, FLinearColor(1.f, 0.82f, 0.30f), 0.95f);
        }
    if (!Arena->IsReferencePreview() && !Arena->IsSyntheticTest() && !Arena->IsOnline() && Training)
    {
        Label(Training->GetJustFrameLabel(Arena->GetPawnData(Controlled)), Margin, 136 * Scale,
              FLinearColor(1.f, 0.85f, 0.35f), 0.66f);
        Label(Training->GetFrameAdvantageLabel(), W * 0.5f + 20 * Scale, 136 * Scale, FLinearColor(0.55f, 1.f, 0.6f),
              0.66f);
    }

    if (Arena->IsReferencePreview())
    {
        Label(Arena->GetReferenceLabel(0), Margin, 162 * Scale, White, 0.75f);
        Label(Arena->GetReferenceLabel(1), W * 0.60f, 162 * Scale, White, 0.75f);
    }

    if (Arena->ShowTrainingPanel())
    {
        const float FooterY = H - 164 * Scale;
        DrawRect(Ink, 0, FooterY, W, 164 * Scale);
        Label(Arena->IsReferencePreview()
                  ? TEXT("레퍼런스 미리보기 / 전투 정지")
                  : (Arena->IsSyntheticTest()
                         ? TEXT("합성 테스트 입력")
                         : (Arena->UsesKeyboard() ? TEXT("키보드 입력")
                                                  : (Arena->IsPadConnected(0) ? TEXT("패드 0번 / 연결됨")
                                                                              : TEXT("패드 0번 / 연결 안 됨")))),
              Margin, FooterY + 16 * Scale, White, 0.84f);
        Label(Arena->IsReferencePreview()
                  ? TEXT("전체 모션 미리보기 / 전투 일시정지")
                  : (Arena->IsSyntheticTest()
                         ? TEXT("LP / RP / LK / RK  -  결정적 테스트 입력")
                         : (Arena->UsesKeyboard() ? TEXT("W A S D  방향     U I J K  공격")
                                                  : TEXT("방향 패드 / 왼쪽 스틱  방향     X Y A B  공격"))),
              Margin, FooterY + 44 * Scale, Muted, 0.70f);
        Label(TEXT("1 키보드   2 패드   3 더미 가드   6 캐릭터 전환   R 리셋   Esc / Start 일시정지"), Margin,
              FooterY + 75 * Scale, Muted, 0.63f);
        Label(Arena->IsReferencePreview() ? TEXT("5 전투로 돌아가기")
                                          : *FString::Printf(TEXT("%s / %s / 5 전체 클립 미리보기 / 중립·뒤로 가드"),
                                                             *PawnText(0, &UFighterPawnData::CommandHint),
                                                             *PawnText(1, &UFighterPawnData::CommandHint)),
              Margin, FooterY + 100 * Scale, Muted, 0.60f);
        Label(FString::Printf(TEXT("조작 %s / %s / %s / 4 진단   %s"), *Arena->GetCharacterLabel(Controlled),
                              *PawnText(0, &UFighterPawnData::StartupSummary),
                              *PawnText(1, &UFighterPawnData::StartupSummary),
                              Training ? *Training->GetDummyAttackLabel() : TEXT("")),
              Margin, FooterY + 124 * Scale, Muted, 0.60f);
        const TCHAR* Names[4] = {TEXT("LP"), TEXT("RP"), TEXT("LK"), TEXT("RK")};
        const float ButtonsX = W - Margin - 252 * Scale;

        for (int32 Index = 0; Index < 4; ++Index)
        {
            const bool bHeld = (Inputs[Controlled].attacks & (1 << Index)) != 0;
            const float X = ButtonsX + Index * 62 * Scale;
            DrawRect(bHeld ? FLinearColor(0.12f, 0.62f, 0.82f) : FLinearColor(0.08f, 0.13f, 0.19f), X,
                     FooterY + 20 * Scale, 52 * Scale, 38 * Scale);
            Label(Names[Index], X + 12 * Scale, FooterY + 28 * Scale, bHeld ? White : Muted, 0.78f);
        }

        Label(FString::Printf(TEXT("방향 %d, %d    상대 %s"), Inputs[Controlled].horizontal,
                              Inputs[Controlled].vertical,
                              Arena->IsPadConnected(1)
                                  ? TEXT("게임패드")
                                  : (Training && Training->IsDummyGuarding()
                                         ? (Training->IsDummyCrouching() ? TEXT("앉아 가드") : TEXT("가드"))
                                         : (Training && Training->IsDummyCrouching() ? TEXT("앉기") : TEXT("대기")))),
              ButtonsX, FooterY + 77 * Scale, Muted, 0.66f);

        if (Training)
            Label(FString::Printf(TEXT("0 카운터: %s%s"),
                                  Training->GetCounterMode() == 1   ? TEXT("켜기")
                                  : Training->GetCounterMode() == 2 ? TEXT("끄기")
                                                                    : TEXT("자동"),
                                  Arena->IsPadConnected(1) ? TEXT(" (2P: 자동)") : TEXT("")),
                  ButtonsX, FooterY + 104 * Scale, Muted, 0.62f);
    }
    else
        Label(TEXT("Tab 조작 안내   H 히트박스"), Margin, H - 30 * Scale, Muted, 0.5f);

    if (Arena->ShowDiagnostics())
    {
        Label(FString::Printf(TEXT("FRAME %u   60 Hz   STALL TIME %.3fs"), State.frame, Arena->GetDroppedWallSeconds()),
              Margin, 162 * Scale, Muted, 0.66f);
        Label(FString::Printf(TEXT("P1 x=%d  attack=%u:%u   P2 x=%d  attack=%u:%u"), State.fighters[0].position_x,
                              State.fighters[0].active_attack, State.fighters[0].attack_frame,
                              State.fighters[1].position_x, State.fighters[1].active_attack,
                              State.fighters[1].attack_frame),
              Margin, 187 * Scale, Muted, 0.62f);
        Label(FString::Printf(TEXT("COMMAND %u   QUEUE %u:%u   GAS EVENTS %d"),
                              uint16(State.fighters[0].active_command), uint16(State.fighters[0].pending_command),
                              State.fighters[0].pending_frames,
                              Arena->GetAbilityHost(0) ? Arena->GetAbilityHost(0)->GetActivationCount() : 0),
              Margin, 211 * Scale, Muted, 0.62f);
    }

    if (Arena->IsOnline())
        DrawRoundPresentation(W, H, Scale);
    FString Overlay;

    if (!Arena->HasInputFocus() && !Arena->IsOnline())
        Overlay = TEXT("화면을 클릭하면 조작할 수 있습니다");
    else if (Arena->IsPauseMenuOpen())
    {
        DrawLobby(W, H, Scale);

        return;
    }
    else if (Arena->IsTrainingPaused())
        Overlay = TEXT("일시정지   /   Esc 또는 Start로 계속");

    if (!Overlay.IsEmpty())
    {
        DrawRect(FLinearColor(0.02f, 0.03f, 0.05f, 0.9f), W * 0.20f, H * 0.45f, W * 0.60f, 60 * Scale);
        Label(Overlay, W * 0.24f, H * 0.45f + 18 * Scale, White, 0.85f);
    }
}

UTexture2D* AFighterHUD::LobbyTexture(const TCHAR* Name)
{
    if (const auto* Found = LobbyTextures.Find(Name))
        return Found->Get();
    UTexture2D* Loaded = LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("/Game/LocalPresentation/UI/%s"), Name));

    if (!Loaded && FString(Name).StartsWith(TEXT("Makuai/")))
    {
        const FString Short = FString(Name).RightChop(7);
        Loaded = LoadObject<UTexture2D>(
            nullptr, *FString::Printf(TEXT("/Game/LocalPresentation/UI/Makuai/%s.%s"), *Short, *Short));
    }

    if (!Loaded && FString(Name).StartsWith(TEXT("GameMessage/")))
    {
        const FString Short = FString(Name).RightChop(12);
        Loaded = LoadObject<UTexture2D>(
            nullptr, *FString::Printf(TEXT("/Game/LocalPresentation/UI/GameMessage/%s.%s"), *Short, *Short));
    }

    LobbyTextures.Add(Name, Loaded);

    return Loaded;
}

bool AFighterHUD::PrepareVsTextures(modular_fighter::CharacterId Left, modular_fighter::CharacterId Right, bool bUrban)
{
    const TCHAR* Codes[3] = {TEXT(""), TEXT("grl"), TEXT("grf")};
    TArray<FString> Names = {TEXT("Makuai/T_UI_Makuai_BGKemuri"),
                             TEXT("Makuai/T_UI_Makuai_CenterLight"),
                             TEXT("Makuai/T_UI_MA_VS"),
                             TEXT("Makuai/T_UI_MA_Flare"),
                             TEXT("Makuai/T_UI_Makuai_ScreenBG"),
                             TEXT("Makuai/T_UI_Makuai_BGchain"),
                             TEXT("Makuai/T_UI_Makuai_BGblush"),
                             TEXT("Makuai/T_UI_Makuai_StatsBG_Default"),
                             TEXT("Makuai/T_UI_Makuai_VS2_Light"),
                             TEXT("Makuai/T_UI_Makuai_VS2"),
                             TEXT("Makuai/T_UI_Makuai_StageName_Plate"),
                             TEXT("Makuai/T_UI_Makuai_Hinoko"),
                             TEXT("Makuai/T_UI_Makuai_ScreenEffect_dust"),
                             bUrban ? TEXT("Makuai/T_UI_Makuai_Stage_st02_NeoCity")
                                    : TEXT("Makuai/T_UI_Makuai_Stage_st03_Island")};

    for (int32 Side = 0; Side < 2; ++Side)
    {
        const TCHAR* Code = Codes[FMath::Clamp(int32(Side == 0 ? Left : Right), 0, 2)];
        Names.Add(FString::Printf(TEXT("Makuai/T_UI_Makuai_Character_Name_%s"), Code));
        Names.Add(FString::Printf(TEXT("Makuai/T_UI_Makuai_Character_%s_%s"), Side == 0 ? TEXT("L") : TEXT("R"), Code));
        Names.Add(FString::Printf(TEXT("T_UI_CS_Character_Name_%s"), Code));
    }

    bool bReady = true;

    for (const FString& Name : Names)
        if (UTexture2D* T = LobbyTexture(*Name))
        {
            T->SetForceMipLevelsToBeResident(30.f);
            bReady &= T->IsFullyStreamedIn();
        }
    return bReady;
}

void AFighterHUD::DrawMenuFade(float W, float H)
{
    using EPractice = AFighterArena::EPracticePhase;
    const int32 Screen = static_cast<int32>(Arena->GetPracticePhase()) * 10 + Arena->GetPracticeStep();
    const double Now = GetWorld()->GetTimeSeconds();
    const int32 TitleScreen = static_cast<int32>(EPractice::Title) * 10,
                MenuScreen = static_cast<int32>(EPractice::MainMenu) * 10;

    const bool bSeamless = Screen == TitleScreen || Screen == static_cast<int32>(EPractice::Opening) * 10 ||
                           (Screen == MenuScreen && LastMenuScreen == TitleScreen);

    if (Screen != LastMenuScreen)
    {
        LastMenuScreen = Screen;
        MenuScreenTime = bSeamless ? -10.0 : Now;
    }

    const float Alpha = 1.f - static_cast<float>((Now - MenuScreenTime) / 0.16);

    if (Alpha > 0.f)
        DrawRect(FLinearColor(0.f, 0.f, 0.f, Alpha * Alpha), 0, 0, W, H);
}

EFighterLobbyAction AFighterHUD::FindLobbyButton(const FVector2D& Point) const
{
    for (const auto& Button : LobbyButtons)
        if (Button.Box.IsInside(Point))
            return Button.Action;

    return EFighterLobbyAction::None;
}

// 온라인 로비 화면을 그린다.
void AFighterHUD::DrawLobby(float W, float H, float Scale)
{
    namespace NetMsg = modular_fighter::net;
    LobbyButtons.Reset();
    const UFighterNetSession* Net = Arena->GetNet();
    const float Time = static_cast<float>(GetWorld()->GetTimeSeconds());
    const FLinearColor White(0.96f, 0.97f, 1.f), Muted(0.66f, 0.7f, 0.78f), Red(0.94f, 0.12f, 0.24f),
        Blue(0.1f, 0.55f, 1.f);
    auto Text = [&](const FString& Value, float X, float Y, const FLinearColor& Color, float Size, int32 Align = 1,
                    bool bTitle = false)
    {
        UFont* Font = bTitle && TitleFont ? TitleFont.Get() : BodyFont ? BodyFont.Get() : LobbyFont.Get();
        const float FontScale = Scale * Size * 15.f / 16.f;
        float TW = 0, TH = 0;
        GetTextSize(Value, TW, TH, Font, FontScale);
        const float DX = Align == 1 ? -TW * 0.5f : Align == 2 ? -TW : 0.f;
        const float DY = -TH * 0.5f;
        DrawText(Value, FLinearColor(0, 0, 0, 0.65f * Color.A), X + DX + 2 * Scale, Y + DY + 2 * Scale, Font, FontScale,
                 false);
        DrawText(Value, Color, X + DX, Y + DY, Font, FontScale, false);
    };
    auto Tex = [&](const TCHAR* Name, float X, float Y, float TW, float TH,
                   const FLinearColor& Tint = FLinearColor::White, EBlendMode Blend = BLEND_Translucent)
    {
        if (UTexture2D* T = LobbyTexture(Name))
            DrawTexture(T, X, Y, TW, TH, 0, 0, 1, 1, Tint, Blend);
    };
    FVector2D Mouse(-1, -1);

    if (PlayerOwner)
    {
        float MX, MY;

        if (PlayerOwner->GetMousePosition(MX, MY))
            Mouse = FVector2D(MX, MY);
    }

    auto Button = [&](float X, float Y, float BW, float BH, EFighterLobbyAction Action)
    {
        const FBox2D Box(FVector2D(X, Y), FVector2D(X + BW, Y + BH));
        LobbyButtons.Add({Box, Action});

        return Box.IsInside(Mouse);
    };
    const TCHAR* Slug[3] = {TEXT(""), TEXT("grl"), TEXT("grf")};
    auto SlugOf = [&](modular_fighter::CharacterId C)
    {
        return Slug[FMath::Clamp(static_cast<int32>(C), 0, 2)];
    };

    auto DrawLoading = [&]() -> bool
    {
        const auto Load = Arena->GetMatchLoad();
        const bool bRoomLoading = Net->HasRoom() && Net->GetRoom().phase == modular_fighter::net::Phase::Loading;

        if (Load == AFighterArena::EMatchLoad::None && !bRoomLoading)
            return false;
        DrawRect(FLinearColor(0.004f, 0.005f, 0.01f, 1.f), 0, 0, W, H);
        const float PW = 760 * Scale, PH = PW * 184.f / 1024.f, PX = (W - PW) * 0.5f, PY = H * 0.42f;
        Tex(TEXT("T_UI_LM_Plate"), PX, PY, PW, PH);
        Tex(TEXT("T_UI_LM_Plate_Add"), PX, PY, PW, PH, FLinearColor(1, 1, 1, 0.5f + 0.3f * FMath::Sin(Time * 3.f)),
            BLEND_Additive);
        const int32 Dots = static_cast<int32>(Time * 2.f) % 4;
        const bool bWaiting = Load == AFighterArena::EMatchLoad::WaitingPeer;
        Text(FString(bWaiting ? TEXT("상대의 로딩을 기다리는 중") : TEXT("스테이지 로딩 중")) +
                 FString::ChrN(Dots, TEXT('.')),
             W * 0.5f, PY + PH * 0.5f, White, 1.8f, 1, true);
        const bool bUrban =
            Arena->GetStagePresentation() && Arena->GetStagePresentation()->GetStageKey() == TEXT("urban");

        if (bWaiting)
            Text(bUrban ? TEXT("얼반 스퀘어") : TEXT("야쿠시마"), W * 0.5f, PY + PH + 18 * Scale, Muted, 0.9f);

        return true;
    };

    const float MenuDt = FMath::Min(GetWorld()->GetDeltaSeconds(), 0.05f);
    auto Ease = [&](int32 Key, bool bFocus)
    {
        float& Value = MenuAnim.FindOrAdd(Key, bFocus ? 1.f : 0.f);
        Value = FMath::Lerp(Value, bFocus ? 1.f : 0.f, 1.f - FMath::Exp(-16.f * MenuDt));

        return Value;
    };

    if (Arena->IsPauseMenuOpen())
    {
        DrawRect(FLinearColor(0.004f, 0.006f, 0.012f, 0.72f), 0, 0, W, H);
        Text(TEXT("일시 정지"), W * 0.5f, H * 0.2f, White, 2.2f, 1, true);
        const FString MatchLabel =
            Arena->IsPracticeMatching() ? FString(TEXT("매칭 취소")) : FString(TEXT("매칭 시작"));

        struct FEntry
        {
            FString Label;
            EFighterLobbyAction Action;
        };

        const FEntry Entries[5] = {{TEXT("연습 계속"), EFighterLobbyAction::PauseResume},
                                   {TEXT("캐릭터·맵 다시 선택"), EFighterLobbyAction::PauseReselect},
                                   {MatchLabel, EFighterLobbyAction::PauseMatching},
                                   {TEXT("메인 메뉴로"), EFighterLobbyAction::PauseMainMenu},
                                   {TEXT("게임 종료"), EFighterLobbyAction::PauseQuit}};
        const float EW = 460 * Scale, EH = 54 * Scale, EX = (W - EW) * 0.5f, EY = H * 0.3f, Step = EH + 6 * Scale;

        PauseCursorEased =
            FMath::Lerp(PauseCursorEased, float(Arena->GetMenuCursor()), 1.f - FMath::Exp(-22.f * MenuDt));
        Tex(TEXT("T_UI_PL_Cursor"), EX, EY + PauseCursorEased * Step, EW, EH);

        for (int32 I = 0; I < 5; ++I)
        {
            const float Y = EY + I * Step;
            const float A = Ease(100 + I, Arena->GetMenuCursor() == I);
            Button(EX, Y, EW, EH, Entries[I].Action);
            Tex(TEXT("T_UI_PL_Menu_Line"), EX, Y + EH + 2 * Scale, EW, 2 * Scale);

            Text(Entries[I].Label, EX + (28 + 12 * A) * Scale, Y + EH * 0.5f, FMath::Lerp(Muted, White, A), 1.25f, 0,
                 true);
        }

        Text(TEXT("↑↓ 선택 · U/Enter 결정 · Esc 돌아가기"), W * 0.5f, EY + 5 * Step + 34 * Scale, Muted, 1.f, 1, true);

        return;
    }

    using EPractice = AFighterArena::EPracticePhase;
    const EPractice Practice = Arena->GetPracticePhase();

    const float Cover = FMath::Max(W / 16.f, H / 9.f), CoverX = (W - Cover * 16.f) * 0.5f,
                CoverY = (H - Cover * 9.f) * 0.5f;
    const float PhaseSeconds = Arena->GetPracticeSeconds();
    auto DrawEmbers = [&](float Alpha)
    {
        const float Rise = FMath::Fmod(Time * 0.06f, 1.f);
        Tex(TEXT("Makuai/T_UI_Makuai_Hinoko"), 0, H * (0.15f - Rise * 0.3f), W, H * 1.2f,
            FLinearColor(1, 0.4f, 0.3f, Alpha), BLEND_Additive);
    };

    if (Practice == EPractice::Opening)
    {
        DrawRect(FLinearColor::Black, 0, 0, W, H);

        if (UTexture* Movie = Arena->GetOpeningTexture())
            DrawTexture(Movie, 0, 0, W, H, 0, 0, 1, 1, FLinearColor::White, BLEND_Opaque);

        if (PhaseSeconds > 1.5f)
            Text(TEXT("아무 버튼: 건너뛰기"), W - 40 * Scale, H - 34 * Scale, FLinearColor(1, 1, 1, 0.55f), 0.9f, 2,
                 true);

        return;
    }

    if (Practice == EPractice::Title)
    {
        Tex(TEXT("T_FG_Title_Backdrop"), CoverX, CoverY, Cover * 16.f, Cover * 9.f, FLinearColor::White, BLEND_Opaque);
        DrawEmbers(FMath::Min(PhaseSeconds / 2.f, 1.f) * 0.5f);
        const float Pulse = 0.55f + 0.45f * FMath::Cos(FMath::Max(PhaseSeconds - 0.6f, 0.f) * 3.2f);

        if (PhaseSeconds > 0.6f)
            Text(TEXT("아무 버튼이나 누르세요"), W * 0.5f, H * 0.72f, FLinearColor(1, 1, 1, Pulse), 1.5f, 1, true);
        const float CopyW = 552 * Scale * 0.8f;
        Tex(TEXT("T_UI_TT_copy"), (W - CopyW) * 0.5f, H - 40 * Scale, CopyW, CopyW * 28.f / 552.f,
            FLinearColor(1, 1, 1, 0.8f));

        return;
    }

    if (Practice == EPractice::MainMenu || Practice == EPractice::Searching)
    {
        const float MenuIn = FMath::Clamp(PhaseSeconds / 0.5f, 0.f, 1.f);
        Tex(TEXT("T_FG_Title_Backdrop_NoLogo"), CoverX, CoverY, Cover * 16.f, Cover * 9.f, FLinearColor::White,
            BLEND_Opaque);

        if (MenuIn < 1.f)
            Tex(TEXT("T_FG_Title_Backdrop"), CoverX, CoverY, Cover * 16.f, Cover * 9.f,
                FLinearColor(1, 1, 1, 1.f - MenuIn));
        DrawEmbers(0.5f);
        Tex(TEXT("T_UI_MainMenu_Bg_Gradation"), 0, 0, W * 0.8f, H, FLinearColor(1, 1, 1, MenuIn));
        const float HeaderX = 152.f / 3.f * Scale, HeaderY = 152.f / 3.f * Scale;
        Text(TEXT("메인 메뉴"), HeaderX, HeaderY + 14 * Scale, White, 2.1f, 0, true);
        Tex(TEXT("T_UI_MM_Banner_Cursor_Gra"), HeaderX, HeaderY + 58 * Scale, 360 * Scale, 3 * Scale);
    }

    if (Practice == EPractice::MainMenu)
    {
        const float TileW = 520 * Scale, TileH = TileW * 292.f / 700.f, TileY = 420.f / 3.f * Scale + 40 * Scale,
                    TileX = 262.f / 3.f * Scale, Gap = 34 * Scale;
        const int32 MenuFocus = Arena->GetMenuCursor();

        struct FTile
        {
            const TCHAR* Art;
            const TCHAR* Icon;
            const TCHAR* Label;
            const TCHAR* Sub;
            EFighterLobbyAction Action;
        };

        const FTile Tiles[2] = {{TEXT("T_UI_SS_Stage_Load_st03_Island"), nullptr, TEXT("연습 모드"),
                                 TEXT("캐릭터와 맵을 골라 혼자 연습합니다"), EFighterLobbyAction::MenuPractice},
                                {TEXT("T_UI_SS_Stage_Load_st02_NeoCity"), TEXT("T_UI_MM_Shortcut_Icon_Matching"),
                                 TEXT("온라인 모드"), TEXT("상대를 찾아 온라인으로 대전합니다"),
                                 EFighterLobbyAction::MenuMultiplayer}};

        for (int32 I = 0; I < 2; ++I)
        {
            const float BaseX = TileX + I * (TileW + Gap);
            const float A = Ease(I, MenuFocus == I);
            Button(BaseX, TileY, TileW, TileH, Tiles[I].Action);

            const float Grow = 1.f + 0.04f * A, DW = TileW * Grow, DH = TileH * Grow, X = BaseX - (DW - TileW) * 0.5f,
                        Y = TileY - (DH - TileH) * 0.5f;

            if (UTexture2D* T = LobbyTexture(Tiles[I].Art))
            {
                const float VExtent = (16.f / 9.f) / (700.f / 292.f);
                const float Shade = FMath::Lerp(0.38f, 0.95f, A);
                DrawTexture(T, X, Y, DW, DH, 0, (1.f - VExtent) * 0.5f, 1, VExtent,
                            FLinearColor(Shade, Shade, Shade, 1.f), BLEND_Opaque);
            }

            DrawRect(FLinearColor(0, 0, 0, FMath::Lerp(0.62f, 0.45f, A)), X, Y + DH * 0.5f, DW, DH * 0.5f);

            if (Tiles[I].Icon)
                Tex(Tiles[I].Icon, X + DW - 66 * Scale, Y + 12 * Scale, 54 * Scale, 54 * Scale,
                    FLinearColor(1, 1, 1, FMath::Lerp(0.55f, 1.f, A)));
            Text(Tiles[I].Label, X + (20 + 8 * A) * Scale, Y + DH * 0.58f, FMath::Lerp(Muted, White, A), 1.8f, 0, true);
            Text(Tiles[I].Sub, X + (20 + 8 * A) * Scale, Y + DH * 0.82f,
                 FMath::Lerp(Muted, FLinearColor(0.86f, 0.88f, 0.92f), A), 0.85f, 0);

            const float MX = DW * 16.f / 700.f, MY = DH * 16.f / 292.f;
            Tex(TEXT("T_UI_MM_Banner_Frame"), X, Y, DW, DH, FLinearColor(1, 1, 1, 1.f - A));
            Tex(TEXT("T_UI_MM_Banner_Cursor"), X - MX, Y - MY, DW + 2 * MX, DH + 2 * MY,
                FLinearColor(1, 1, 1, A * (0.8f + 0.2f * FMath::Sin(Time * 5.f))));
        }

        const float QY = H - 400.f / 3.f * Scale + 20 * Scale, QX = 144.f / 3.f * Scale, QW = 300 * Scale,
                    QH = 54 * Scale;
        const float QA = Ease(2, MenuFocus == 2);
        Button(QX, QY, QW, QH, EFighterLobbyAction::MenuQuit);

        Tex(TEXT("T_UI_PL_Cursor"), QX, QY, QW * QA, QH, FLinearColor(1, 1, 1, QA));
        Tex(TEXT("T_UI_MM_Shortcut_Icon_Quit"), QX + 12 * Scale, QY + 7 * Scale, 40 * Scale, 40 * Scale,
            FLinearColor(1, 1, 1, FMath::Lerp(0.6f, 1.f, QA)));
        Text(TEXT("게임 종료"), QX + (66 + 8 * QA) * Scale, QY + QH * 0.5f, FMath::Lerp(Muted, White, QA), 1.2f, 0,
             true);
        Text(TEXT("←→↓ 선택 · U/Enter 결정 · J/Esc 타이틀로 · 마우스 가능"), W * 0.5f, H - 34 * Scale, Muted, 1.f, 1,
             true);

        return;
    }

    if (Practice == EPractice::Searching)
    {
        const float PW = 760 * Scale, PH = PW * 184.f / 1024.f, PX = (W - PW) * 0.5f, PY = H * 0.42f;
        Tex(TEXT("T_UI_LM_Plate"), PX, PY, PW, PH);
        Tex(TEXT("T_UI_LM_Plate_Add"), PX, PY, PW, PH, FLinearColor(1, 1, 1, 0.5f + 0.3f * FMath::Sin(Time * 3.f)),
            BLEND_Additive);
        const int32 Dots = static_cast<int32>(Time * 2.f) % 4;
        const FString SearchStatus =
            Net->IsConnected() ? FString(TEXT("상대를 찾는 중")) : FString(TEXT("서버에 연결할 수 없습니다"));
        Text(SearchStatus + FString::ChrN(Dots, TEXT('.')), W * 0.5f, PY + PH * 0.5f, White, 1.8f, 1, true);
        Text(TEXT("상대를 찾으면 사이드와 캐릭터 선택 화면으로 넘어갑니다"), W * 0.5f, PY + PH + 18 * Scale, Muted,
             0.95f);
        const float CW = 300 * Scale, CH = 54 * Scale, CX = W * 0.5f - CW * 0.5f, CY = H - 76 * Scale;
        const bool bCancelHover = Button(CX, CY, CW, CH, EFighterLobbyAction::MenuCancelSearch);
        const float CA = Ease(10, bCancelHover);
        Tex(TEXT("T_UI_PL_Cursor"), CX, CY, CW * CA, CH, FLinearColor(1, 1, 1, CA));
        Text(TEXT("J 또는 Esc: 취소"), W * 0.5f, CY + CH * 0.5f, FMath::Lerp(Muted, White, CA), 1.1f, 1, true);

        return;
    }

    if (Practice == EPractice::StageSelect)
    {
        DrawRect(FLinearColor(0.008f, 0.01f, 0.016f, 1.f), 0, 0, W, H);
        const uint8 Choice = Arena->GetStageChoice();

        struct FStage
        {
            const TCHAR* Panel;
            const TCHAR* Load;
            const TCHAR* Name;
            uint8 Code;
            EFighterLobbyAction Action;
        };

        const FStage Stages[2] = {{TEXT("T_FG_SS_Panel_st02_NeoCity"), TEXT("T_UI_SS_Stage_Load_st02_NeoCity"),
                                   TEXT("얼반 스퀘어"), 1, EFighterLobbyAction::MenuStageUrban},
                                  {TEXT("T_FG_SS_Panel_st03_Island"), TEXT("T_UI_SS_Stage_Load_st03_Island"),
                                   TEXT("야쿠시마"), 0, EFighterLobbyAction::MenuStageYakushima}};

        for (int32 I = 0; I < 2; ++I)
            Tex(Stages[I].Load, (W - Cover * 16.f) * 0.5f, (H - Cover * 9.f) * 0.5f, Cover * 16.f, Cover * 9.f,
                FLinearColor(1, 1, 1, Ease(20 + I, Choice == Stages[I].Code)));
        Tex(TEXT("T_UI_SS_Stage_BG_shadow"), 0, 0, W, H);
        Text(TEXT("연습 모드 · 맵 선택"), W * 0.5f, 34 * Scale, White, 1.7f, 1, true);

        const float FW = 410 * Scale * 0.9f, FH = 218 * Scale * 0.9f, PGap = 40 * Scale, PanelY = H - FH - 118 * Scale;

        for (int32 I = 0; I < 2; ++I)
        {
            const float BaseX = W * 0.5f - FW - PGap * 0.5f + I * (FW + PGap);
            const float A = Ease(30 + I, Choice == Stages[I].Code);
            Button(BaseX, PanelY, FW, FH, Stages[I].Action);
            const float Grow = 1.f + 0.05f * A, DW = FW * Grow, DH = FH * Grow, FX = BaseX - (DW - FW) * 0.5f,
                        FY = PanelY - (DH - FH) * 0.5f;
            const float PW = DW * 340.f / 410.f, PH = DH * 180.f / 218.f, PX = FX + (DW - PW) * 0.5f,
                        PY = FY + (DH - PH) * 0.5f;
            const float Shade = FMath::Lerp(0.45f, 1.f, A);
            Tex(TEXT("T_UI_SS_Stage_Panel_shadow"), FX, FY, DW, DH);
            Tex(TEXT("T_UI_SS_Stage_Panel_SelectBG"), FX, FY, DW, DH, FLinearColor(A, A, A, 1.f), BLEND_Additive);
            Tex(Stages[I].Panel, PX, PY, PW, PH, FLinearColor(Shade, Shade, Shade, 1.f));
            Tex(TEXT("T_UI_SS_Stage_Panel_SelectRim"), FX, FY, DW, DH,
                FLinearColor(1, 1, 1, A * (0.8f + 0.2f * FMath::Sin(Time * 5.f))));
            Text(Stages[I].Name, BaseX + FW * 0.5f, PanelY + FH + 22 * Scale, FMath::Lerp(Muted, White, A), 1.5f, 1,
                 true);
        }

        Text(TEXT("←→ 선택 · U/Enter 결정 · J/Esc 뒤로"), W * 0.5f, H - 34 * Scale, White, 1.1f, 1, true);

        return;
    }

    const bool bMatchedLobby = Practice == EPractice::Matched && !Net->GetResult();

    if (bMatchedLobby && DrawLoading())
        return;

    if (bMatchedLobby)
    {
        DrawRect(FLinearColor(0.004f, 0.005f, 0.01f, 1.f), 0, 0, W, H);
        const int32 Dots = static_cast<int32>(Arena->GetPracticeSeconds() * 2.f) % 4;
        Text(TEXT("대전 상대를 찾았습니다"), W * 0.5f, H * 0.46f, White, 2.f, 1, true);
        Text(FString(TEXT("대전을 준비하는 중")) + FString::ChrN(Dots, TEXT('.')), W * 0.5f, H * 0.46f + 56 * Scale,
             Muted, 1.f, 1, true);

        return;
    }

    if (Practice == EPractice::Select)
    {
        DrawRect(FLinearColor(0.93f, 0.93f, 0.95f, 1.f), 0, 0, W, H);
        const FLinearColor Ink(0.08f, 0.09f, 0.12f), InkMuted(0.35f, 0.37f, 0.42f);
        const int32 Step = Arena->GetPracticeStep();
        const float Art = H * 0.92f;

        for (int32 Side = 0; Side < 2; ++Side)
        {
            const auto C = Arena->GetPracticePick(Side);

            if (Side == 1 && Step == 0)
                continue;
            const TCHAR* Code = SlugOf(C);
            const float AX = Side == 0 ? W * 0.25f - Art * 0.5f : W * 0.75f - Art * 0.5f;
            Tex(*FString::Printf(TEXT("T_UI_CS_BG_Character_%s_%s"), Side == 0 ? TEXT("L") : TEXT("R"), Code), AX,
                H * 0.02f, Art, Art);
            const float NW = W * 0.5f, NH = NW * 420.f / 1920.f, NX = Side == 0 ? 0 : W - NW, NY = H - NH - 6 * Scale;
            Tex(*FString::Printf(TEXT("T_UI_CS_BG_Character_Name_%s"), Code), NX, NY, NW, NH,
                FLinearColor(1, 1, 1, 0.55f));
            const float LH = 64 * Scale, LW = LH * (C == modular_fighter::CharacterId::Kazuya ? 444.f : 288.f) / 136.f;
            const float LX = Side == 0 ? 36 * Scale : W - 36 * Scale - LW, LY = H * 0.12f;
            Tex(*FString::Printf(TEXT("T_UI_CS_Character_Name_%s"), Code), LX, LY, LW, LH);
            const bool bDone = Side < Step;
            Text(Side == 0 ? (bDone ? TEXT("1P 결정") : TEXT("1P 선택 중")) : TEXT("연습 상대 선택 중"),
                 Side == 0 ? 40 * Scale : W - 40 * Scale, LY + LH + 4 * Scale,
                 bDone ? Red : (Side == 0 ? InkMuted : Blue), 1.1f, Side == 0 ? 0 : 2, true);
        }

        Text(TEXT("연습 모드 · 캐릭터 선택"), W * 0.5f, 34 * Scale, Ink, 1.7f, 1, true);
        const modular_fighter::CharacterId Columns[2] = {modular_fighter::CharacterId::Kazuya,
                                                         modular_fighter::CharacterId::Paul};
        const float TW = 128 * Scale, TH = TW * 192.f / 160.f, Gap = 14 * Scale, TY = H - TH - 70 * Scale;

        for (int32 Column = 0; Column < 2; ++Column)
        {
            const float X = W * 0.5f - TW - Gap * 0.5f + Column * (TW + Gap);
            const bool bHere = Arena->GetPracticePick(Step) == Columns[Column];
            const TCHAR* Code = SlugOf(Columns[Column]);
            Button(X, TY, TW, TH, Column == 0 ? EFighterLobbyAction::PickKazuya : EFighterLobbyAction::PickPaul);
            const float ThumbFocus = Ease(40 + Column, bHere);
            const float SW = TW * (1.f + 0.07f * ThumbFocus), SH = TH * (1.f + 0.07f * ThumbFocus),
                        SX = X - (SW - TW) * 0.5f, SY = TY - (SH - TH) * 0.5f;
            Tex(bHere ? TEXT("Panel_Selected_Base") : TEXT("Panel_Normal_Base"), SX, SY, SW, SH);
            Tex(*(bHere ? FString::Printf(TEXT("T_UI_CS_Character_Thumb_Selected_%s"), Code)
                        : FString::Printf(TEXT("T_UI_CS_Character_Thumb_Normal_%s"), Code)),
                SX, SY, SW, SH);

            if (bHere)
            {
                const TCHAR* P = Step == 0 ? TEXT("P1") : TEXT("P2");
                Tex(*FString::Printf(TEXT("Panel_%s_Selected_OuterGlow"), P), SX - SW * 0.2f, SY - SH * 0.167f,
                    SW * 1.4f, SH * 1.333f, FLinearColor(1, 1, 1, 0.75f + 0.25f * FMath::Sin(Time * 5.f)));
                Tex(*FString::Printf(TEXT("Panel_%s_Selected_Text_%s"), P, P), SX, SY, SW, SH);
            }
        }

        Text(Step == 0 ? TEXT("←→ 선택 · U 결정") : TEXT("←→ 연습 상대 선택 · U 결정 · J 뒤로"), W * 0.5f,
             H - 40 * Scale, Ink, 1.1f, 1, true);

        return;
    }

    DrawRect(FLinearColor(0.008f, 0.01f, 0.016f, 0.86f), 0, 0, W, H);

    if (!Net->HasRoom())
    {
        const float PW = 760 * Scale, PH = PW * 184.f / 1024.f, PX = (W - PW) * 0.5f, PY = H * 0.42f;
        Tex(TEXT("T_UI_LM_Plate"), PX, PY, PW, PH);
        Tex(TEXT("T_UI_LM_Plate_Add"), PX, PY, PW, PH, FLinearColor(1, 1, 1, 0.5f + 0.3f * FMath::Sin(Time * 3.f)),
            BLEND_Additive);
        const int32 Dots = static_cast<int32>(Time * 2.f) % 4;
        const FString Status =
            Net->IsConnected() ? FString(TEXT("상대를 찾는 중")) : FString(TEXT("서버에 연결할 수 없습니다"));
        Text(Status + FString::ChrN(Dots, TEXT('.')), W * 0.5f, PY + PH * 0.5f, White, 1.8f, 1, true);
        Text(TEXT("온라인 대전 · 롤백 넷코드"), W * 0.5f, PY + PH + 18 * Scale, Muted, 0.9f);

        return;
    }

    const auto& Room = Net->GetRoom();
    const auto& Me = Room.slots[Room.your_slot];
    const auto& Them = Room.slots[1 - Room.your_slot];
    const int32 Cursor = Arena->GetLobbyCursor();

    if (Net->GetResult())
    {
        const auto& R = *Net->GetResult();
        const uint8 MySide = Me.side == NetMsg::Side::Left ? 0 : 1;
        const bool bWin = R.winner == MySide, bDraw = R.winner == 2;
        Tex(TEXT("T_UI_RS_Eff_PanelGlow_Add"), W * 0.5f - 420 * Scale, H * 0.08f, 840 * Scale, 666 * Scale,
            (bWin ? Red : Blue) * FLinearColor(1, 1, 1, 0.55f), BLEND_Additive);
        Text(bDraw  ? TEXT("무승부")
             : bWin ? TEXT("승리")
                    : TEXT("패배"),
             W * 0.5f, H * 0.24f, bWin ? FLinearColor(1.f, 0.82f, 0.35f) : White, 3.2f, 1, true);
        const TCHAR* Status = R.status == NetMsg::ResultStatus::Agreed
                                  ? TEXT("두 클라이언트의 결과가 일치합니다 (상태 해시 동일)")
                              : R.status == NetMsg::ResultStatus::Desync ? TEXT("동기화 불일치 - 상태 해시가 다릅니다")
                                                                         : TEXT("상대의 연결이 끊어졌습니다");
        Text(Status, W * 0.5f, H * 0.24f + 62 * Scale, Muted, 0.9f);
        Text(FString::Printf(TEXT("롤백 %llu회 · 대기 %u프레임"), Net->GetRollbacks(), Net->GetStalls()), W * 0.5f,
             H * 0.24f + 88 * Scale, Muted, 0.8f);
        const bool bClosed = Room.phase == NetMsg::Phase::Closed;
        const TArray<EFighterLobbyAction> Menu = Arena->GetResultMenu();
        auto ResultLabel = [](EFighterLobbyAction Action)
        {
            switch (Action)
            {
            case EFighterLobbyAction::Rematch:
                return TEXT("리매치");
            case EFighterLobbyAction::Requeue:
                return TEXT("다시 매칭");
            case EFighterLobbyAction::ResultPractice:
                return TEXT("연습 모드로");
            default:
                return TEXT("메인 메뉴로");
            }
        };
        const float BW = 461 * Scale, BH = 70 * Scale, MenuY = H * 0.5f, MenuStep = BH + 10 * Scale;

        for (int32 I = 0; I < Menu.Num(); ++I)
        {
            const float BX = (W - BW) * 0.5f, BY = MenuY + I * MenuStep;
            const bool bHover = Button(BX, BY, BW, BH, Menu[I]);
            const bool bFocus = bHover || Cursor == I;
            Tex(TEXT("T_UI_RS_Button_Hover"), BX, BY, BW, BH, FLinearColor(1, 1, 1, bFocus ? 1.f : 0.35f));

            if (bFocus)
                Tex(TEXT("T_UI_RS_Button_Hover_Add"), BX, BY, BW, BH, FLinearColor(1, 1, 1, 0.9f));
            Text(ResultLabel(Menu[I]), BX + BW * 0.5f, BY + BH * 0.5f, White, 1.4f, 1, true);
        }

        if (!bClosed && (Me.rematch || Them.rematch))
            Text(Me.rematch ? TEXT("상대의 응답을 기다리는 중") : TEXT("상대가 리매치를 원합니다"), W * 0.5f,
                 MenuY + Menu.Num() * MenuStep + 10 * Scale, FLinearColor(1.f, 0.82f, 0.35f), 0.9f);

        if (bClosed)
            Text(TEXT("상대가 나갔습니다"), W * 0.5f, MenuY + Menu.Num() * MenuStep + 10 * Scale, Muted, 0.9f);

        return;
    }

    if (DrawLoading())
        return;

    if (Room.phase == NetMsg::Phase::SideSelect)
    {
        const bool bPicked = Me.side != NetMsg::Side::None;

        for (int32 Column = 0; Column < 2; ++Column)
        {
            const auto Side = Column == 0 ? NetMsg::Side::Left : NetMsg::Side::Right;
            const bool bHover = Button(Column == 0 ? 0 : W * 0.5f, 0, W * 0.5f, H,
                                       Column == 0 ? EFighterLobbyAction::SideLeft : EFighterLobbyAction::SideRight);
            const bool bLit = bPicked ? Net->GetViewSide() == Side : (bHover || Cursor == Column);
            Tex(Column == 0 ? TEXT("T_UI_SideSelect_L") : TEXT("T_UI_SideSelect_R"), 0, 0, W, H,
                FLinearColor(1, 1, 1, bLit ? 1.f : 0.45f));
            const float CH = H * 0.7f, CW = CH * (Column == 0 ? 912.f : 880.f) / 1536.f;
            const float CX = Column == 0 ? W * 0.25f - CW * 0.5f : W * 0.75f - CW * 0.5f, CY = H * 0.22f;

            if (bLit)
                Tex(TEXT("T_UI_SideSelect_Glow_add"), CX - CW * 0.4f, CY - CH * 0.05f, CW * 1.8f, CH * 1.1f,
                    (Column == 0 ? Red : Blue) * FLinearColor(1, 1, 1, 0.8f), BLEND_Additive);
            Tex(Column == 0 ? TEXT("T_UI_SideSelect_L_chara") : TEXT("T_UI_SideSelect_R_chara"), CX, CY, CW, CH,
                FLinearColor(1, 1, 1, bLit ? 0.95f : 0.3f));
            const float TX = Column == 0 ? W * 0.25f : W * 0.75f;
            Text(Column == 0 ? TEXT("왼쪽") : TEXT("오른쪽"), TX, H * 0.08f, White, 2.4f, 1, true);
            Text(Column == 0 ? TEXT("내 캐릭터가 화면 왼쪽에 섭니다") : TEXT("내 캐릭터가 화면 오른쪽에 섭니다"), TX,
                 H * 0.1f + 56 * Scale, Muted, 1.1f);

            if (bPicked && Net->GetViewSide() == Side)
                Text(TEXT("선택 완료"), TX, H * 0.84f, FLinearColor(1.f, 0.85f, 0.4f), 2.f, 1, true);
        }

        Tex(TEXT("T_UI_SideSelect_Line_Light"), W * 0.5f - 46 * Scale, 0, 92 * Scale, H, FLinearColor(1, 1, 1, 0.8f),
            BLEND_Additive);
        Text(bPicked ? TEXT("상대를 기다리는 중") : TEXT("클릭하거나 방향키로 고른 뒤 U로 결정"), W * 0.5f, H * 0.93f,
             White, 1.f, 1, true);

        return;
    }

    if (Room.phase == NetMsg::Phase::CharacterSelect)
    {
        const modular_fighter::CharacterId Columns[2] = {modular_fighter::CharacterId::Kazuya,
                                                         modular_fighter::CharacterId::Paul};

        const bool bMeLeft = Net->GetViewSide() != NetMsg::Side::Right;
        auto Shown = [&](const NetMsg::SlotView& S)
        {
            return S.ready ? S.character : Columns[Cursor];
        };
        DrawRect(FLinearColor(0.93f, 0.93f, 0.95f, 1.f), 0, 0, W, H);
        const FLinearColor Ink(0.08f, 0.09f, 0.12f), InkMuted(0.35f, 0.37f, 0.42f);
        const float Art = H * 0.92f;
        {
            const int32 Side = bMeLeft ? 0 : 1;
            const auto C = Shown(Me);
            const TCHAR* Code = SlugOf(C);
            const float AX = Side == 0 ? W * 0.25f - Art * 0.5f : W * 0.75f - Art * 0.5f;
            Tex(*FString::Printf(TEXT("T_UI_CS_BG_Character_%s_%s"), Side == 0 ? TEXT("L") : TEXT("R"), Code), AX,
                H * 0.02f, Art, Art);

            const float NW = W * 0.5f, NH = NW * 420.f / 1920.f, NX = Side == 0 ? 0 : W - NW, NY = H - NH - 6 * Scale;
            Tex(*FString::Printf(TEXT("T_UI_CS_BG_Character_Name_%s"), Code), NX, NY, NW, NH,
                FLinearColor(1, 1, 1, 0.55f));
            const float LH = 64 * Scale, LW = LH * (C == modular_fighter::CharacterId::Kazuya ? 444.f : 288.f) / 136.f;
            const float LX = Side == 0 ? 36 * Scale : W - 36 * Scale - LW, LY = H * 0.12f;
            Tex(*FString::Printf(TEXT("T_UI_CS_Character_Name_%s"), Code), LX, LY, LW, LH);
            const TCHAR* State = Me.ready ? TEXT("준비 완료 · 상대를 기다리는 중") : TEXT("선택 중");
            Text(State, Side == 0 ? 40 * Scale : W - 40 * Scale, LY + LH + 4 * Scale,
                 Me.ready ? (Side == 0 ? Red : Blue) : InkMuted, 1.1f, Side == 0 ? 0 : 2, true);
        }

        Text(TEXT("캐릭터 선택"), W * 0.5f, 34 * Scale, Ink, 1.7f, 1, true);
        Text(TEXT("상대"), bMeLeft ? W * 0.75f : W * 0.25f, H * 0.42f, InkMuted, 2.4f, 1, true);
        Text(TEXT("상대의 캐릭터는 VS 화면에서 공개됩니다"), bMeLeft ? W * 0.75f : W * 0.25f, H * 0.42f + 44 * Scale,
             InkMuted, 1.f, 1, true);

        {
            const TCHAR* Names[2] = {TEXT("야쿠시마"), TEXT("얼반 스퀘어")};
            const float SW = 170 * Scale, SH = 40 * Scale, SY = 64 * Scale;

            for (int32 Stage = 0; Stage < 2; ++Stage)
            {
                const float SX = W * 0.5f - SW - 6 * Scale + Stage * (SW + 12 * Scale);
                const bool bHover = Button(
                    SX, SY, SW, SH, Stage == 0 ? EFighterLobbyAction::StageYakushima : EFighterLobbyAction::StageUrban);
                const bool bMine = Me.stage == Stage;
                DrawRect(bMine ? FLinearColor(0.08f, 0.09f, 0.12f, 0.9f)
                               : FLinearColor(0.08f, 0.09f, 0.12f, bHover ? 0.35f : 0.18f),
                         SX, SY, SW, SH);
                Text(Names[Stage], SX + SW * 0.5f, SY + SH * 0.5f, bMine ? White : Ink, 1.f, 1, true);
            }

            Text(Me.ready ? TEXT("스테이지") : TEXT("스테이지  ↑↓"), W * 0.5f, SY + SH + 14 * Scale, InkMuted, 0.85f, 1,
                 true);
        }

        const float TW = 128 * Scale, TH = TW * 192.f / 160.f, Gap = 14 * Scale, TY = H - TH - 70 * Scale;

        for (int32 Column = 0; Column < 2; ++Column)
        {
            const float X = W * 0.5f - TW - Gap * 0.5f + Column * (TW + Gap);
            const bool bHover =
                Button(X, TY, TW, TH, Column == 0 ? EFighterLobbyAction::PickKazuya : EFighterLobbyAction::PickPaul);
            const bool bMineHere = Shown(Me) == Columns[Column];
            const bool bSelected = bMineHere || bHover;
            const TCHAR* Code = SlugOf(Columns[Column]);
            Tex(bSelected ? TEXT("Panel_Selected_Base") : TEXT("Panel_Normal_Base"), X, TY, TW, TH);
            Tex(*(bSelected ? FString::Printf(TEXT("T_UI_CS_Character_Thumb_Selected_%s"), Code)
                            : FString::Printf(TEXT("T_UI_CS_Character_Thumb_Normal_%s"), Code)),
                X, TY, TW, TH);

            if (bMineHere)
            {
                const TCHAR* P = bMeLeft ? TEXT("P1") : TEXT("P2");
                const float GX = X - TW * 0.2f, GY = TY - TH * 0.167f;
                Tex(*FString::Printf(TEXT("Panel_%s_Selected_OuterGlow"), P), GX, GY, TW * 1.4f, TH * 1.333f,
                    FLinearColor(1, 1, 1, 0.75f + 0.25f * FMath::Sin(Time * 5.f)));
                Tex(*FString::Printf(TEXT("Panel_%s_Selected_InnerGlow"), P), X, TY, TW, TH,
                    FLinearColor(1, 1, 1, 0.35f));
                Tex(*FString::Printf(TEXT("Panel_%s_Selected_Text_%s"), P, P), X, TY, TW, TH);
            }
        }

        const float BW = 300 * Scale, BH = 52 * Scale, BX = (W - BW) * 0.5f, BY = H - 60 * Scale;
        const bool bReadyHover =
            Button(BX, BY, BW, BH, Me.ready ? EFighterLobbyAction::Unready : EFighterLobbyAction::Ready);
        Tex(TEXT("T_UI_RS_Button_Hover"), BX, BY, BW, BH, FLinearColor(1, 1, 1, bReadyHover ? 1.f : 0.75f));

        if (bReadyHover)
            Tex(TEXT("T_UI_RS_Button_Hover_Add"), BX, BY, BW, BH, FLinearColor(1, 1, 1, 0.9f));
        Text(Me.ready ? TEXT("선택 취소  J") : TEXT("준비 완료  U"), W * 0.5f, BY + BH * 0.5f, White, 1.1f, 1, true);
    }
}

// 라운드 시작과 종료 연출을 그린다.
void AFighterHUD::DrawRoundPresentation(float W, float H, float Scale)
{
    namespace NetMsg = modular_fighter::net;
    const UFighterNetSession* Net = Arena->GetNet();

    const bool bPractice = Arena->GetPracticePhase() == AFighterArena::EPracticePhase::GetReady;
    NetMsg::MatchStart PracticeRound;

    if (bPractice)
    {
        PracticeRound.match_id = 0x50524143u;
        PracticeRound.left = Arena->GetPracticePick(0);
        PracticeRound.right = Arena->GetPracticePick(1);
    }
    else if (!Net->GetRound())
        return;
    const auto& Round = bPractice ? PracticeRound : *Net->GetRound();
    const uint32 Frame = bPractice        ? static_cast<uint32>(Arena->GetPracticeSeconds() * 60.f)
                         : Net->InMatch() ? Net->GetState().frame
                                          : 0;
    const uint32 Intro = NetMsg::RoundIntroFrames(Round.round);
    const bool bFinal = Round.left_wins + 1 >= Round.rounds_to_win && Round.right_wins + 1 >= Round.rounds_to_win;
    const FString RoundKey = FString::Printf(TEXT("%u:%u"), Round.match_id, Round.round);

    if (RoundKey != CueRound)
    {
        CueRound = RoundKey;
        FiredCues.Reset();
    }

    auto Cue = [&](const TCHAR* Name, const TCHAR* Sound)
    {
        if (FiredCues.Contains(Name))
            return;
        FiredCues.Add(Name);

        if (USoundBase* S = LoadObject<USoundBase>(
                nullptr, *FString::Printf(TEXT("/Game/LocalPresentation/Audio/Narration/NA_%s"), Sound)))
            UGameplayStatics::PlaySound2D(this, S);
    };
    auto Message = [&](const TCHAR* Name, float CX, float CY, float MW, float MH, float Alpha, bool bAdditive = false)
    {
        if (UTexture2D* T = LobbyTexture(*FString::Printf(TEXT("GameMessage/%s"), Name)))
            DrawTexture(T, CX - MW * 0.5f, CY - MH * 0.5f, MW, MH, 0, 0, 1, 1, FLinearColor(1, 1, 1, Alpha),
                        bAdditive ? BLEND_Additive : BLEND_Translucent);
    };
    auto Ease = [](float T)
    {
        T = FMath::Clamp(T, 0.f, 1.f);

        return 1.f - (1.f - T) * (1.f - T);
    };
    auto Text = [&](const FString& Value, float X, float Y, const FLinearColor& Color, float Size, int32 Align = 1,
                    bool bTitle = false)
    {
        UFont* Font = bTitle && TitleFont ? TitleFont.Get() : BodyFont ? BodyFont.Get() : LobbyFont.Get();
        const float FontScale = Scale * Size * 15.f / 16.f;
        float TW = 0, TH = 0;
        GetTextSize(Value, TW, TH, Font, FontScale);
        const float DX = Align == 1 ? -TW * 0.5f : Align == 2 ? -TW : 0.f;
        DrawText(Value, FLinearColor(0, 0, 0, 0.65f * Color.A), X + DX + 2 * Scale, Y - TH * 0.5f + 2 * Scale, Font,
                 FontScale, false);
        DrawText(Value, Color, X + DX, Y - TH * 0.5f, Font, FontScale, false);
    };

    const bool bSwapVs = !bPractice && Arena->IsViewFlipped();
    const modular_fighter::CharacterId Chars[2] = {bSwapVs ? Round.right : Round.left,
                                                   bSwapVs ? Round.left : Round.right};
    const TCHAR* Codes[3] = {TEXT(""), TEXT("grl"), TEXT("grf")};

    auto Back = [](float T)
    {
        T = FMath::Clamp(T, 0.f, 1.f);
        const float C = 1.70158f;

        return 1.f + (C + 1.f) * FMath::Pow(T - 1.f, 3) + C * FMath::Pow(T - 1.f, 2);
    };
    auto Shake = [&](float T, float Amount)
    {
        if (T < 0)
            return FVector2D::ZeroVector;
        const float D = FMath::Max(0.f, 1.f - T / 0.3f);

        return FVector2D(FMath::Sin(T * 90.f), FMath::Cos(T * 70.f)) * Amount * D * Scale;
    };
    float VsZoom = 1.f;
    auto Makuai = [&](const FString& Name, float X, float Y, float TW, float TH, const FLinearColor& Tint,
                      EBlendMode Blend = BLEND_Translucent, float U = 0, float UL = 1)
    {
        UTexture2D* T = LobbyTexture(*(TEXT("Makuai/") + Name));

        if (T)
            DrawTexture(T, W * 0.5f + (X - W * 0.5f) * VsZoom, H * 0.5f + (Y - H * 0.5f) * VsZoom, TW * VsZoom,
                        TH * VsZoom, U, 0, UL, 1, Tint, Blend);
    };

    if (Round.round == 1 && Frame < NetMsg::VsScreenFrames)
    {
        const float F = float(Frame), T = F / 60.f;
        DrawRect(FLinearColor::Black, 0, 0, W, H);
        const float Exit = FMath::Clamp((F - 204.f) / 18.f, 0.f, 1.f);
        VsZoom = 1.f + 0.12f * Exit;
        const float Wash =
            F < 58.f ? FMath::Clamp((F - 50.f) / 8.f, 0.f, 1.f) : 1.f - FMath::Clamp((F - 58.f) / 12.f, 0.f, 1.f);

        if (F < 60.f)
        {
            const float In = FMath::Clamp((F - 12.f) / 10.f, 0.f, 1.f),
                        Warm = FMath::Clamp((F - 36.f) / 12.f, 0.f, 1.f);
            const float Drift = 1.f + 0.04f * FMath::Clamp((F - 12.f) / 48.f, 0.f, 1.f);

            DrawRect(FLinearColor(0.015f, 0.02f, 0.035f, 1), 0, 0, W, H);
            Makuai(TEXT("T_UI_Makuai_BGKemuri"), 0, 0, W, H, FLinearColor(0.06f, 0.09f, 0.14f, In), BLEND_Additive,
                   T * 0.02f, 0.8f);
            DrawRect(FLinearColor(0.4f, 0.02f, 0.05f, 0.5f * Warm * In), 0, 0, W, H);
            Makuai(TEXT("T_UI_Makuai_CenterLight"), W * 0.5f - H, -H * 0.5f, H * 2.f, H * 2.f,
                   FLinearColor(0.9f, 0.15f, 0.2f, 0.5f * Warm), BLEND_Additive);

            for (int32 Side = 0; Side < 2; ++Side)
            {
                const float BW = W * 0.56f * Drift, BH = BW * 1280.f / 2800.f;
                const float X = Side == 0 ? W * 0.02f : W * 0.44f, Y = Side == 0 ? H * 0.04f : H * 0.4f;
                Makuai(FString::Printf(TEXT("T_UI_Makuai_Character_Name_%s"),
                                       Codes[FMath::Clamp(int32(Chars[Side]), 0, 2)]),
                       X - (Drift - 1.f) * BW * 0.5f, Y, BW, BH, FLinearColor(0.95f, 0.12f, 0.32f, 0.95f * In));
            }

            const float VSz = H * 0.09f;
            Makuai(TEXT("T_UI_MA_VS"), W * 0.5f - VSz * 0.5f, H * 0.47f - VSz * 0.5f, VSz, VSz,
                   FLinearColor(0.95f, 0.2f, 0.4f, 0.45f * In));

            const float Wipe = FMath::Clamp((F - 46.f) / 12.f, 0.f, 1.f);

            if (Wipe > 0)
                Makuai(TEXT("T_UI_MA_Flare"), FMath::Lerp(-W * 1.1f, W * 0.2f, Wipe), H * 0.1f, W * 1.6f, H * 0.8f,
                       FLinearColor(1, 0.7f, 0.6f, Wipe), BLEND_Additive);
        }
        else
        {
            Makuai(TEXT("T_UI_Makuai_ScreenBG"), 0, 0, W, H, FLinearColor::White);
            DrawRect(FLinearColor(0.02f, 0.f, 0.02f, 0.55f), 0, 0, W, H);
            Makuai(TEXT("T_UI_Makuai_BGKemuri"), 0, 0, W, H, FLinearColor(0.5f, 0.16f, 0.24f, 0.5f), BLEND_Additive,
                   FMath::Fmod(T * 0.02f, 1.f), 0.8f);
            Makuai(TEXT("T_UI_Makuai_BGchain"), 0, 0, W, H, FLinearColor(0.7f, 0.3f, 0.4f, 0.6f), BLEND_Additive);
            Makuai(TEXT("T_UI_Makuai_BGblush"), 0, 0, W, H, FLinearColor(1, 1, 1, 0.55f));
            Makuai(TEXT("T_UI_Makuai_CenterLight"), W * 0.5f - H * 0.6f, -H * 0.1f, H * 1.2f, H * 1.2f,
                   FLinearColor(1, 1, 1, 0.5f), BLEND_Additive);

            for (int32 Side = 0; Side < 2; ++Side)
            {
                const TCHAR* Code = Codes[FMath::Clamp(int32(Chars[Side]), 0, 2)];
                const float BW = W * 0.62f, BH = BW * 0.5f;
                Makuai(FString::Printf(TEXT("T_UI_Makuai_Character_Name_%s"), Code),
                       (Side == 0 ? -W * 0.06f : W * 0.44f), H * 0.02f, BW, BH, FLinearColor(0.75f, 0.06f, 0.2f, 0.7f));
                const float Size = H * 1.05f, Lit = 1.f + 0.6f * Wash;
                Makuai(FString::Printf(TEXT("T_UI_Makuai_Character_%s_%s"), Side == 0 ? TEXT("L") : TEXT("R"), Code),
                       Side == 0 ? W * 0.02f : W * 0.98f - Size, H * 0.02f, Size, Size, FLinearColor(Lit, Lit, Lit, 1));

                const float Open = Ease((F - 66.f - Side * 3.f) / 14.f);
                const float LH = 46 * Scale,
                            LW = LH * (Chars[Side] == modular_fighter::CharacterId::Kazuya ? 444.f : 288.f) / 136.f;
                const float PW = FMath::Lerp(LH * 0.5f, LW + 40 * Scale, Open), PY = H * 0.745f;
                const float PX = Side == 0 ? W * 0.03f : W * 0.97f - PW;

                if (F >= 64.f)
                    Makuai(TEXT("T_UI_Makuai_StatsBG_Default"), PX, PY + LH * 0.62f, PW, LH * 0.38f,
                           FLinearColor(1.f, 0.1f, 0.3f, 0.9f));

                if (UTexture2D* N = LobbyTexture(*FString::Printf(TEXT("T_UI_CS_Character_Name_%s"), Code)))
                {
                    const float NX = Side == 0 ? W * 0.03f + 12 * Scale : W * 0.97f - 12 * Scale - LW;
                    const float ZX = W * 0.5f + (NX - W * 0.5f) * VsZoom, ZY = H * 0.5f + (PY - H * 0.5f) * VsZoom;
                    DrawTexture(N, ZX, ZY, LW * VsZoom, LH * VsZoom, 0, 0, 1, 1,
                                FLinearColor(1, 1, 1, FMath::Clamp((Open - 0.6f) / 0.4f, 0.f, 1.f)), BLEND_Translucent);
                }
            }

            const float VW = 230 * Scale, VH = VW * 0.62f;
            Makuai(TEXT("T_UI_Makuai_VS2_Light"), W * 0.5f - VW * 0.9f, H * 0.42f - VH * 0.9f, VW * 1.8f, VH * 1.8f,
                   FLinearColor(1, 1, 1, 0.6f + 0.4f * Wash), BLEND_Additive);
            Makuai(TEXT("T_UI_Makuai_VS2"), W * 0.5f - VW * 0.5f, H * 0.42f - VH * 0.5f, VW, VH, FLinearColor::White);
            const float PW = 520 * Scale, PH = 92 * Scale, PX = W * 0.5f - PW * 0.5f, PY = H * 0.80f;
            const bool bUrban = Arena->IsVsStageUrban();
            Makuai(bUrban ? TEXT("T_UI_Makuai_Stage_st02_NeoCity") : TEXT("T_UI_Makuai_Stage_st03_Island"),
                   PX - 60 * Scale, PY - 30 * Scale, PW + 120 * Scale, PH + 80 * Scale, FLinearColor(1, 1, 1, 0.85f));
            Makuai(TEXT("T_UI_Makuai_StageName_Plate"), PX, PY + 22 * Scale, PW, 44 * Scale, FLinearColor::White);
            auto ZY = [&](float Y)
            {
                return H * 0.5f + (Y - H * 0.5f) * VsZoom;
            };
            Text(TEXT("스테이지"), W * 0.5f, ZY(PY + 6 * Scale), FLinearColor(0.9f, 0.92f, 0.95f, 1), 0.85f * VsZoom, 1,
                 true);
            Text(bUrban ? TEXT("얼반 스퀘어") : TEXT("야쿠시마"), W * 0.5f, ZY(PY + 44 * Scale),
                 FLinearColor(1.f, 0.18f, 0.38f, 1), 1.3f * VsZoom, 1, true);
            const float Rise = FMath::Fmod(T * 0.08f, 1.f);
            Makuai(TEXT("T_UI_Makuai_Hinoko"), 0, H * (0.15f - Rise * 0.3f), W, H * 1.2f,
                   FLinearColor(1, 0.45f, 0.35f, 0.7f), BLEND_Additive);
            Makuai(TEXT("T_UI_Makuai_ScreenEffect_dust"), 0, 0, W, H, FLinearColor(1, 1, 1, 0.35f), BLEND_Additive);
        }

        VsZoom = 1.f;

        if (Wash > 0)
            DrawRect(FLinearColor(1, 0.97f, 0.95f, 0.92f * Wash), 0, 0, W, H);
        const float Dark = F >= 222.f ? 1.f : Exit;

        if (Dark > 0)
            DrawRect(FLinearColor(0, 0, 0, Dark), 0, 0, W, H);

        if (Frame >= 12)
            Cue(TEXT("getready"), TEXT("GSN_Get_ready_for_the_next_battle"));
    }

    if (bPractice)
        return;

    if (Round.round == 1 && Frame >= NetMsg::VsScreenFrames && Frame < NetMsg::VsScreenFrames + 12)
        DrawRect(FLinearColor(0, 0, 0, 1.f - (Frame - NetMsg::VsScreenFrames) / 12.f), 0, 0, W, H);

    const uint32 RoundStart = NetMsg::RoundCallFrame(Round.round);
    const uint32 ReadyStart = RoundStart + NetMsg::ReadyOffset, FightStart = RoundStart + NetMsg::FightOffset;
    const float MCY = H * 0.505f;

    auto Letters = [&](const TCHAR* Prefix, float F, float SnapAt, float Gap, float CenterX, float Alpha)
    {
        const bool bSolid = F >= SnapAt;
        const float LW = 158 * Scale * (bSolid ? 1.f : 1.06f);
        const float A = Alpha * (bSolid ? 1.f : 0.35f * FMath::Clamp(F / 4.f, 0.f, 1.f));

        for (int32 I = 0; I < 5; ++I)
            Message(*FString::Printf(TEXT("%s%02d_alp"), Prefix, I + 1), CenterX + (I - 2) * Gap * Scale, MCY, LW, LW,
                    A);
    };

    if (Frame >= RoundStart && Frame < RoundStart + 42)
    {
        const float F = float(Frame - RoundStart);
        const float Alpha = 1.f - FMath::Clamp((F - 36.f) / 6.f, 0.f, 1.f);
        const float RoundX = bFinal ? W * 0.5f + 131 * Scale : W * 0.5f;
        Letters(TEXT("T_UI_GM_Round"), F, 10.f, 62.f, RoundX, Alpha);
        const float Sweep = FMath::Clamp((F - 10.f) / 20.f, 0.f, 1.f);

        if (Sweep > 0 && Sweep < 1)
            Message(TEXT("T_UI_GM_Round_add"), RoundX + (Sweep - 0.5f) * 300 * Scale, MCY, 300 * Scale, 150 * Scale,
                    (1.f - FMath::Abs(Sweep - 0.5f) * 2.f) * 0.7f * Alpha, true);
        const float NT = (F - 7.f) / 5.f;

        if (NT > 0)
        {
            const float NS = FMath::Lerp(1.6f, 1.f, Ease(NT)), NA = FMath::Clamp(NT * 2.f, 0.f, 1.f) * Alpha;

            if (bFinal)
                Message(TEXT("T_UI_GM_RoundFinal_alp"), W * 0.5f - 168 * Scale, MCY, 320 * Scale * NS, 160 * Scale * NS,
                        NA);
            else
            {
                const int32 Num = FMath::Clamp<int32>(Round.round, 1, 5);
                const float NX = W * 0.5f + 219 * Scale, NB = 179 * Scale * NS;
                Message(*FString::Printf(TEXT("T_UI_GM_RoundNum%d_add"), Num), NX, MCY, NB, NB,
                        NA * 0.6f * (1.f - FMath::Clamp(NT - 1.f, 0.f, 1.f) * 0.5f), true);
                Message(*FString::Printf(TEXT("T_UI_GM_RoundNum%d_alp"), Num), NX, MCY, NB, NB, NA);
            }
        }

        static const TCHAR* RoundCall[5] = {TEXT("Round1"), TEXT("Round2"), TEXT("Round3"), TEXT("Round4"),
                                            TEXT("Round5")};
        Cue(TEXT("round"), bFinal ? TEXT("FinalRound") : RoundCall[FMath::Clamp<int32>(Round.round, 1, 5) - 1]);
    }

    if (Frame >= ReadyStart && Frame < FightStart + 9)
        Letters(TEXT("T_UI_GM_Ready"), float(Frame - ReadyStart), 6.f, 59.f, W * 0.5f, 1.f);

    if (Frame >= FightStart && Frame < FightStart + 44)
    {
        const float F = float(Frame - FightStart);
        const float FW = 372 * Scale, FH = 186 * Scale;
        const FVector2D Jolt = Shake((F - 9.f) / 60.f, 5.f);

        if (F < 10.f)
        {
            const float S = FMath::Lerp(2.6f, 1.25f, Ease(F / 8.f));
            Message(TEXT("T_UI_GM_FightBlur"), W * 0.5f, MCY, FW * S, FH * S,
                    0.9f * FMath::Clamp((F + 1.f) / 3.f, 0.f, 1.f) * (F < 8.f ? 1.f : 0.5f));
        }

        if (F >= 8.f)
        {
            const float Out = FMath::Clamp((F - 38.f) / 6.f, 0.f, 1.f);
            const float S = FMath::Lerp(1.25f, 1.f, Ease((F - 8.f) / 3.f));
            const float Burst = 1.f - FMath::Clamp((F - 9.f) / 16.f, 0.f, 1.f);
            UTexture2D* FightAdd = LobbyTexture(TEXT("GameMessage/T_UI_GM_Fight_add"));

            if (Burst > 0 && FightAdd)
                DrawTexture(FightAdd, W * 0.5f - FW * 0.6f + Jolt.X, MCY - FH * 0.6f + Jolt.Y, FW * 1.2f, FH * 1.2f, 0,
                            0, 1, 1, FLinearColor(1.f, 0.75f, 0.45f, Burst), BLEND_Additive);
            const float Streak = FMath::Clamp((F - 10.f) / 6.f, 0.f, 1.f);
            const float StreakA = F < 10.f ? 0.f : F < 20.f ? 1.f : 1.f - FMath::Clamp((F - 20.f) / 14.f, 0.f, 1.f);

            if (StreakA > 0)
            {
                const float LW = FMath::Lerp(W * 0.3f, W * 1.8f, Ease(Streak)) / 0.75f,
                            LH = FMath::Lerp(150.f, 50.f, FMath::Clamp((F - 10.f) / 14.f, 0.f, 1.f)) * Scale / 0.14f;
                Message(TEXT("T_UI_GM_SmashLine_add"), W * 0.5f, MCY, LW, LH, StreakA, true);
            }

            Message(TEXT("T_UI_GM_Fight_alp"), W * 0.5f + Jolt.X, MCY + Jolt.Y, FW * S, FH * S, 1.f - Out);
        }

        if (F >= 6.f)
            Cue(TEXT("fight"), TEXT("Fight"));
    }

    const modular_fighter::State* Confirmed = Net->GetConfirmedState();

    if (Confirmed)
    {
        const bool bKo = Confirmed->fighters[0].hp == 0 || Confirmed->fighters[1].hp == 0;
        const bool bTime = !bKo && Confirmed->frame >= Net->GetRoundFrameLimit();

        if ((bKo || bTime) && KoFrame < 0)
            KoFrame = int64(Confirmed->frame);

        if (!(bKo || bTime))
            KoFrame = -1;

        if (KoFrame >= 0)
        {
            const float F = float(int64(Confirmed->frame) - KoFrame);
            const float Out = FMath::Clamp((F - 96.f) / 12.f, 0.f, 1.f);
            const FVector2D Jolt = Shake((F - 11.f) / 60.f, 8.f);

            auto Glyph = [&](const TCHAR* Solid, const TCHAR* Blur, float X, float BW, float BH, float Delay)
            {
                const float L = (F - Delay) / 5.f;

                if (L <= 0)
                    return;
                const float S = FMath::Lerp(2.4f, 1.f, Ease(L)) * (1.f + 0.06f * Out);
                const float BlurA = L < 1.f ? 0.9f * (1.f - L * 0.5f) : 0.6f * Out;

                if (Blur && BlurA > 0)
                    Message(Blur, X + Jolt.X, MCY + Jolt.Y, BW * S * 1.0f, BH * S * 1.0f, BlurA * (1.f - Out * 0.5f));
                Message(Solid, X + Jolt.X, MCY + Jolt.Y, BW * S, BH * S,
                        FMath::Clamp(L * 1.5f - 0.5f, 0.f, 1.f) * (1.f - Out));
            };

            if (bKo)
            {
                const float Glow =
                    F < 11.f ? 0.f : (0.35f + 0.25f * FMath::Max(0.f, 1.f - (F - 11.f) / 20.f)) * (1.f - Out);
                Message(TEXT("T_UI_GM_KO_add"), W * 0.5f + Jolt.X, MCY + Jolt.Y, 584 * Scale, 292 * Scale, Glow, true);
                Glyph(TEXT("T_UI_GM_KO_01_alp"), TEXT("T_UI_GM_KO_01Blur_alp"), W * 0.5f - 77 * Scale, 252 * Scale,
                      252 * Scale, 0.f);
                Glyph(TEXT("T_UI_GM_KO_02_alp"), TEXT("T_UI_GM_KO_02Blur_alp"), W * 0.5f + 77 * Scale, 252 * Scale,
                      252 * Scale, 6.f);
                const auto& FF = Confirmed->fighters;
                const bool bPerfect = FF[0].hp == modular_fighter::InitialHp || FF[1].hp == modular_fighter::InitialHp;
                Cue(TEXT("ko"), FF[0].hp == 0 && FF[1].hp == 0 ? TEXT("DoubleKO") : TEXT("KO"));

                if (bPerfect && F > 60.f)
                {
                    const float P = Ease((F - 60.f) / 8.f);
                    Message(TEXT("T_UI_GM_PERFECT_alp"), W * 0.5f, MCY + 135 * Scale,
                            612 * Scale * FMath::Lerp(1.5f, 1.f, P), 153 * Scale * FMath::Lerp(1.5f, 1.f, P),
                            FMath::Clamp((F - 60.f) / 4.f, 0.f, 1.f) * (1.f - Out));
                    Cue(TEXT("perfect"), TEXT("Perfect"));
                }
            }
            else
            {
                Glyph(TEXT("T_UI_GM_Time_alp"), nullptr, W * 0.5f - 130 * Scale, 504 * Scale, 252 * Scale, 0.f);
                Glyph(TEXT("T_UI_GM_UP_alp"), nullptr, W * 0.5f + 189 * Scale, 252 * Scale, 252 * Scale, 6.f);
                Cue(TEXT("timeup"), TEXT("TimeUp"));
            }
        }
    }
}

// 경기 결과 화면을 그린다.
bool AFighterHUD::DrawMatchEnd(float W, float H, float Scale)
{
    const UFighterNetSession* Net = Arena->GetNet();

    if (!Net->GetResult() || !Net->HasRoom())
    {
        ResultShownAt = -1;

        return false;
    }

    const double Now = GetWorld()->GetTimeSeconds();

    if (ResultShownAt < 0)
        ResultShownAt = Now;
    const double T = Now - ResultShownAt;

    if (T > 3.5)
        return false;
    const auto& R = *Net->GetResult();
    const auto& Room = Net->GetRoom();
    const uint8 MySide = Room.slots[Room.your_slot].side == modular_fighter::net::Side::Left ? 0 : 1;
    const bool bDraw = R.winner == 2, bWin = R.winner == MySide;
    auto Message = [&](const TCHAR* Name, float CX, float CY, float MW, float MH, float Alpha)
    {
        if (UTexture2D* Tex = LobbyTexture(*FString::Printf(TEXT("GameMessage/%s"), Name)))
            DrawTexture(Tex, CX - MW * 0.5f, CY - MH * 0.5f, MW, MH, 0, 0, 1, 1, FLinearColor(1, 1, 1, Alpha),
                        BLEND_Translucent);
    };
    const float A = FMath::Clamp(float(T) * 4.f, 0.f, 1.f);

    const float In = FMath::Clamp(float(T) / 0.3f, 0.f, 1.f);
    const float Back = 1.f + 2.70158f * FMath::Pow(In - 1.f, 3) + 1.70158f * FMath::Pow(In - 1.f, 2);
    const float Slide = (1.f - Back) * W * 0.5f;
    DrawRect(FLinearColor(0, 0, 0, 0.35f * A), 0, 0, W, H);

    const float MCY = H * 0.505f;

    if (bDraw)
        Message(TEXT("T_UI_GM_Draw_alp"), W * 0.5f, MCY, 490 * Scale * FMath::Lerp(1.5f, 1.f, Back),
                245 * Scale * FMath::Lerp(1.5f, 1.f, Back), A);
    else
    {
        Message(TEXT("T_UI_GM_You_alp"), W * 0.5f - 175 * Scale - Slide, MCY, 490 * Scale, 245 * Scale, A);
        Message(bWin ? TEXT("T_UI_GM_Win_alp") : TEXT("T_UI_GM_Lose_alp"),
                W * 0.5f + (bWin ? 139.f : 175.f) * Scale + Slide, MCY, 490 * Scale, 245 * Scale, A);
    }

    const FString Key = FString::Printf(TEXT("end:%u"), R.match_id);

    if (!FiredCues.Contains(Key))
    {
        FiredCues.Add(Key);

        if (USoundBase* S =
                LoadObject<USoundBase>(nullptr, bDraw  ? TEXT("/Game/LocalPresentation/Audio/Narration/NA_Draw")
                                                : bWin ? TEXT("/Game/LocalPresentation/Audio/Narration/NA_YouWin")
                                                       : TEXT("/Game/LocalPresentation/Audio/Narration/NA_YouLose")))
            UGameplayStatics::PlaySound2D(this, S);
    }

    return true;
}
