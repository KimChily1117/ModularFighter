// 게임 HUD.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "FighterNetSession.h"
#include "FighterT6Feedback.h"
#include "FighterHUD.generated.h"

UCLASS()

class FIGHTERGAME_API AFighterHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    virtual void DrawHUD() override;

    EFighterLobbyAction FindLobbyButton(const FVector2D& Point) const;

    bool PrepareVsTextures(modular_fighter::CharacterId Left, modular_fighter::CharacterId Right, bool bUrban);

private:
    void DrawLobby(float W, float H, float Scale);

    void DrawMenuFade(float W, float H);
    TMap<int32, float> MenuAnim;
    float PauseCursorEased = 0.f;
    int32 LastMenuScreen = -1;
    double MenuScreenTime = -10.0;
    void DrawRoundPresentation(float W, float H, float Scale);
    bool DrawMatchEnd(float W, float H, float Scale);
    FString CueRound;
    TSet<FString> FiredCues;
    int64 KoFrame = -1;
    FighterT6Feedback::BattleGauge HealthTrails[2];
    uint32 HealthLastCoreFrame = 0;
    double ResultShownAt = -1;
    UPROPERTY()
    TObjectPtr<class AFighterArena> Arena;
    UPROPERTY()
    TObjectPtr<class UTexture2D> KazuyaPortrait;
    UPROPERTY()
    TObjectPtr<class UTexture2D> PaulPortrait;
    UPROPERTY()
    TObjectPtr<class UTexture2D> InfiniteTimer;
    UPROPERTY()
    TObjectPtr<class UTexture2D> TimerDigits;
    UPROPERTY()
    TObjectPtr<class UTexture2D> TimerCountdownDigits;
    UPROPERTY()
    TArray<TObjectPtr<class UTexture2D>> GaugeFrames;
    UPROPERTY()
    TArray<TObjectPtr<class UMaterialInterface>> GaugeFills;

    class UTexture2D* LobbyTexture(const TCHAR* Name);
    UPROPERTY()
    TMap<FName, TObjectPtr<class UTexture2D>> LobbyTextures;
    UPROPERTY()
    TObjectPtr<class UFont> LobbyFont;
    UPROPERTY()
    TObjectPtr<class UFont> BodyFont;
    UPROPERTY()
    TObjectPtr<class UFont> TitleFont;

    struct FLobbyButton
    {
        FBox2D Box;
        EFighterLobbyAction Action;
    };

    TArray<FLobbyButton> LobbyButtons;
};
