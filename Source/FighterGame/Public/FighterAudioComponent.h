// 전투 오디오. 롤백에 영향을 주지 않는 표시 계층이다.
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "modular_fighter/combat.hpp"
#include "FighterAudioComponent.generated.h"
class USoundBase;
class UAudioComponent;
UCLASS(ClassGroup = (Fighter))

class FIGHTERGAME_API UFighterAudioComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

    void PlayImpact(const FVector& Position, float Damage, bool bGuard = false, bool bCounter = false);

    int32 GetGuardSoundCount() const
    {
        return Guard.Num();
    }

    void Present(const modular_fighter::State& Before, const modular_fighter::State& After, bool bEmit = true);
    void ResetPresentation();
    void SetRoundScore(int32 Player1Wins, int32 Player2Wins, int32 WinsNeeded = 3);

    static bool UsesClimax(int32 P1, int32 P2, int32 Target = 3)
    {
        return Target > 0 && FMath::Max(P1, P2) >= Target - 1;
    }

    bool IsReady() const
    {
        return !Medium.IsEmpty() && !Heavy.IsEmpty() && NormalMusic && ClimaxMusic;
    }

    void SetStageMusic(const FString& StageKey);
    void PlayMenuMusic(const FString& Key);
    bool IsMusicPlaying() const;
    void PlayBattleMusic();

    FString GetMusicKey() const
    {
        return MusicKey;
    }

private:
    struct FVoice
    {
        uint32 Animation = 0;
        uint16 Frame = 1;
        FString Group;
    };

    struct FCinematicCue
    {
        uint16 Command = 0, Frame = 0;
        uint8 Character = 0;
        bool Finish = false, Voice = false;
        USoundBase* Normal = nullptr;
        USoundBase* Devil = nullptr;
        FString Event;
    };

    void PlayDialogue(int32 Fighter, USoundBase* Sound, const FString& Event, uint32 Frame);
    bool IsDialoguePlaying(int32 Fighter) const;
    UPROPERTY()
    TArray<TObjectPtr<USoundBase>> RetainedSounds;
    UPROPERTY()
    TObjectPtr<USoundBase> NormalMusic;
    UPROPERTY()
    TObjectPtr<USoundBase> ClimaxMusic;
    UPROPERTY()
    TObjectPtr<UAudioComponent> Music;
    UPROPERTY()
    TArray<TObjectPtr<UAudioComponent>> VoicePlayers;
    UPROPERTY()
    TArray<TObjectPtr<UAudioComponent>> DialoguePlayers;
    TArray<FCinematicCue> CinematicCues;
    int32 DialogueSlot[2] = {0, 0};
    TSet<uint64> EmittedCinematics;
    TArray<USoundBase*> Medium, Heavy, Guard;
    int32 LastPick[3] = {-1, -1, -1};
    TMap<FString, TArray<USoundBase*>> Voices;
    TMap<uint32, FVoice> Commands;
    TSet<uint64> EmittedVoices;
    int32 HitIndex = 0, VoiceIndex[2] = {};
    bool bClimax = false;
    TMap<FString, USoundBase*> MusicByKey;
    FString StageMusic = TEXT("yakushima");
    FString MusicKey;
    void PlayMusicKey(const FString& Key);
};
