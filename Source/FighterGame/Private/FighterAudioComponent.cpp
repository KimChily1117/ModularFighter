// 음악, 대사, 타격음 재생.
#include "FighterAudioComponent.h"
#include "Components/AudioComponent.h"
#include "Dom/JsonObject.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundBase.h"
#include "GameFramework/Actor.h"
namespace Core = modular_fighter;

void UFighterAudioComponent::BeginPlay()
{
    Super::BeginPlay();
    FString Text;
    TSharedPtr<FJsonObject> Root;

    if (!FFileHelper::LoadFileToString(Text,
                                       *(FPaths::ProjectDir() / TEXT("LocalData/Presentation/combat-audio.json"))) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root))
        return;
    auto Load = [this](const FString& Path) -> USoundBase*
    {
        if (!Path.StartsWith(TEXT("/Game/LocalPresentation/Audio/")))
            return nullptr;
        auto* Sound = LoadObject<USoundBase>(nullptr, *Path);

        if (Sound)
            RetainedSounds.Add(Sound);

        return Sound;
    };
    auto Array = [&](const TArray<TSharedPtr<FJsonValue>>& Values)
    {
        TArray<USoundBase*> Result;

        for (const auto& V : Values)
            if (auto* S = Load(V->AsString()))
                Result.Add(S);

        return Result;
    };
    Medium = Array(Root->GetArrayField(TEXT("hit_medium")));
    Heavy = Array(Root->GetArrayField(TEXT("hit_heavy")));
    const TArray<TSharedPtr<FJsonValue>>* GuardValues = nullptr;

    if (Root->TryGetArrayField(TEXT("guard"), GuardValues))
        Guard = Array(*GuardValues);
    NormalMusic = Load(Root->GetStringField(TEXT("music_normal")));
    ClimaxMusic = Load(Root->GetStringField(TEXT("music_climax")));

    for (const auto& Owner : Root->GetObjectField(TEXT("voices"))->Values)
        for (const auto& Group : Owner.Value->AsObject()->Values)
            Voices.Add(FString(Owner.Key) + FString(Group.Key), Array(Group.Value->AsArray()));

    for (const auto& V : Root->GetArrayField(TEXT("commands")))
    {
        auto R = V->AsObject();
        FVoice Voice;
        Voice.Animation = static_cast<uint32>(R->GetNumberField(TEXT("animation_key")));
        Voice.Frame = static_cast<uint16>(R->GetIntegerField(TEXT("frame")));
        Voice.Group = FString::FromInt(R->GetIntegerField(TEXT("character"))) + R->GetStringField(TEXT("group"));
        Commands.Add((R->GetIntegerField(TEXT("character")) << 16) | R->GetIntegerField(TEXT("command")), Voice);
    }

    FString CinematicText;
    TSharedPtr<FJsonObject> CinematicRoot;
    int32 ExpectedCues = 0;

    if (FFileHelper::LoadFileToString(CinematicText,
                                      *(FPaths::ProjectDir() / TEXT("LocalData/Presentation/cinematic-audio.json"))) &&
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(CinematicText), CinematicRoot))
        for (const auto& V : CinematicRoot->GetArrayField(TEXT("cues")))
        {
            ++ExpectedCues;
            const auto R = V->AsObject();
            FCinematicCue C;
            C.Character = R->GetIntegerField(TEXT("character"));
            C.Command = R->GetIntegerField(TEXT("command"));
            C.Frame = R->GetIntegerField(TEXT("frame"));
            C.Finish = R->GetStringField(TEXT("phase")) == TEXT("finish");
            C.Voice = R->GetBoolField(TEXT("voice"));
            C.Event = R->GetStringField(TEXT("event"));
            C.Normal = Load(R->GetStringField(TEXT("normal")));
            C.Devil = Load(R->GetStringField(TEXT("devil")));

            if (C.Normal && C.Devil)
                CinematicCues.Add(C);
            else
                UE_LOG(LogTemp, Error, TEXT("FIGHTER_CINEMATIC_AUDIO missing=%s"), *C.Event);
        }
    auto Component = [this]()
    {
        auto* C = NewObject<UAudioComponent>(GetOwner());
        GetOwner()->AddInstanceComponent(C);
        C->bAutoActivate = false;
        C->bAutoDestroy = false;
        C->RegisterComponent();

        return C;
    };

    FString MusicText;
    TSharedPtr<FJsonObject> MusicRoot;

    if (FFileHelper::LoadFileToString(MusicText, *(FPaths::ProjectDir() / TEXT("LocalData/Presentation/music.json"))) &&
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(MusicText), MusicRoot) && MusicRoot.IsValid())
        for (const auto& Row : MusicRoot->Values)
            if (auto* Sound = Load(Row.Value->AsObject()->GetStringField(TEXT("asset"))))
                MusicByKey.Add(FString(Row.Key), Sound);

    if (!MusicByKey.Contains(TEXT("yakushima_normal")) && NormalMusic)
        MusicByKey.Add(TEXT("yakushima_normal"), NormalMusic.Get());

    if (!MusicByKey.Contains(TEXT("yakushima_climax")) && ClimaxMusic)
        MusicByKey.Add(TEXT("yakushima_climax"), ClimaxMusic.Get());
    Music = Component();
    Music->SetVolumeMultiplier(.3f);

    if (!MusicKey.IsEmpty())
    {
        const FString Pending = MusicKey;
        MusicKey.Reset();
        PlayMusicKey(Pending);
    }

    for (int I = 0; I < 2; ++I)
    {
        auto* C = Component();
        C->SetVolumeMultiplier(.7f);
        VoicePlayers.Add(C);
    }

    for (int I = 0; I < 4; ++I)
    {
        auto* C = Component();
        C->SetVolumeMultiplier(.85f);
        DialoguePlayers.Add(C);
    }

    UE_LOG(LogTemp, Display, TEXT("FIGHTER_CINEMATIC_AUDIO ready=%d source_cues=%d"),
           ExpectedCues > 0 && CinematicCues.Num() == ExpectedCues ? 1 : 0, CinematicCues.Num());
    UE_LOG(LogTemp, Display,
           TEXT("FIGHTER_AUDIO ready=%d hits=%d/%d voice_commands=%d music=%s music_keys=%d; selected source media"),
           IsReady() ? 1 : 0, Medium.Num(), Heavy.Num(), Commands.Num(), *MusicKey, MusicByKey.Num());
    UE_LOG(LogTemp, Display, TEXT("FIGHTER_AUDIO guard=%d source=Play_Guard"), Guard.Num());
}

void UFighterAudioComponent::PlayImpact(const FVector& Position, float Damage, bool bGuard, bool bCounter)
{
    const int32 Kind = bGuard ? 2 : (bCounter || Damage >= 20) ? 1 : 0;
    const TArray<USoundBase*>& Pool = Kind == 2 ? Guard : Kind == 1 ? Heavy : Medium;

    if (Pool.IsEmpty())
        return;
    int32 Pick = FMath::RandRange(0, Pool.Num() - 1);

    if (Pool.Num() > 1 && Pick == LastPick[Kind])
        Pick = (Pick + 1 + FMath::RandRange(0, Pool.Num() - 2)) % Pool.Num();
    LastPick[Kind] = Pick;
    ++HitIndex;
    const float Volume =
        Kind == 2 ? .7f : FMath::GetMappedRangeValueClamped(FVector2D(5.f, 30.f), FVector2D(.65f, 1.f), Damage);
    UGameplayStatics::PlaySoundAtLocation(this, Pool[Pick], Position, Volume, FMath::FRandRange(.96f, 1.04f));
}

// 현재 프레임 상태에 맞는 소리를 재생한다.
void UFighterAudioComponent::Present(const Core::State& Before, const Core::State& After, bool bEmit)
{
    if (After.frame <= Before.frame || VoicePlayers.Num() != 2)
        return;

    if (After.frame > 600)
        for (auto It = EmittedVoices.CreateIterator(); It; ++It)
            if ((*It >> 32) + 600 < After.frame)
                It.RemoveCurrent();

    for (int I = 0; I < 2; ++I)
    {
        const auto& A = After.fighters[I];
        const auto& B = Before.fighters[I];
        const bool Finish = A.sequence_role == 1;
        const auto Command = Finish ? A.sequence_command : A.active_command;
        const auto PriorCommand = B.sequence_role == 1 ? B.sequence_command : B.active_command;
        const int32 Frame = Finish ? A.sequence_frame : A.attack_frame;
        const int32 PriorFrame = (PriorCommand == Command && (B.sequence_role == 1) == Finish)
                                     ? (Finish ? B.sequence_frame : B.attack_frame)
                                     : 0;
        const auto* Attack = Core::GetAttackDefinition(A.character, Command);
        const bool Devil = A.character == Core::CharacterId::Kazuya &&
                           (A.heat_frames || (Attack && (Attack->system_action == Core::SystemAction::RageArt ||
                                                         Attack->system_action == Core::SystemAction::HeatSmash)));
        bool HasDedicated = false;

        for (int32 CueIndex = 0; CueIndex < CinematicCues.Num(); ++CueIndex)
        {
            const auto& C = CinematicCues[CueIndex];

            if (C.Character != uint8(A.character) || C.Command != uint16(Command))
                continue;
            HasDedicated |= C.Voice;

            if (C.Finish != Finish || A.sequence_role == 2 || Frame < C.Frame || PriorFrame >= C.Frame)
                continue;
            const uint64 CueKey = (uint64(After.frame) << 32) | (uint64(I) << 24) | uint32(CueIndex);

            if (EmittedCinematics.Contains(CueKey))
                continue;
            EmittedCinematics.Add(CueKey);

            if (!bEmit)
                continue;
            USoundBase* Sound = Devil ? C.Devil : C.Normal;

            if (C.Voice)
                PlayDialogue(I, Sound, C.Event, After.frame);
            else
                UGameplayStatics::PlaySoundAtLocation(this, Sound,
                                                      FVector(A.position_x * .1f, A.position_z * .1f, 120.f), .8f);
            UE_LOG(LogTemp, Display,
                   TEXT("FIGHTER_CINEMATIC_CUE event=%s player=%d finish=%d frame=%d voice=%d devil=%d"), *C.Event, I,
                   Finish ? 1 : 0, Frame, C.Voice ? 1 : 0, Devil ? 1 : 0);
        }

        if (HasDedicated || IsDialoguePlaying(I) || !A.attack_frame)
            continue;
        const auto KeyFor = [](const Core::Fighter& F)
        {
            return (uint32(F.character) << 16) | uint32(F.active_command);
        };
        const auto* Voice = Commands.Find(KeyFor(A));

        if (!Voice)
            continue;
        const auto* Previous = Commands.Find(KeyFor(B));
        const uint16 PriorVoiceFrame =
            B.attack_frame && Previous && Previous->Animation == Voice->Animation ? B.attack_frame : 0;

        if (A.attack_frame < Voice->Frame || PriorVoiceFrame >= Voice->Frame)
            continue;
        const uint64 Key = (uint64(After.frame) << 32) | (uint64(I) << 24) | uint16(A.active_command);

        if (EmittedVoices.Contains(Key))
            continue;
        EmittedVoices.Add(Key);

        if (!bEmit)
            continue;
        const auto* Pool = Voices.Find(Voice->Group);

        if (!Pool || Pool->IsEmpty())
            continue;
        VoicePlayers[I]->Stop();
        VoicePlayers[I]->SetSound((*Pool)[VoiceIndex[I]++ % Pool->Num()]);
        VoicePlayers[I]->Play();
    }

    if (After.frame > 1200)
        for (auto It = EmittedCinematics.CreateIterator(); It; ++It)
            if ((*It >> 32) + 1200 < After.frame)
                It.RemoveCurrent();
}

bool UFighterAudioComponent::IsDialoguePlaying(int32 Fighter) const
{
    for (int32 Slot = 0; Slot < 2; ++Slot)
    {
        const int32 Index = Fighter * 2 + Slot;

        if (DialoguePlayers.IsValidIndex(Index) && DialoguePlayers[Index]->IsPlaying())
            return true;
    }

    return false;
}

void UFighterAudioComponent::PlayDialogue(int32 Fighter, USoundBase* Sound, const FString& Event, uint32 Frame)
{
    constexpr float DialogueFadeSeconds = .08f;
    const int32 Outgoing = Fighter * 2 + DialogueSlot[Fighter];

    if (!DialoguePlayers.IsValidIndex(Outgoing))
        return;
    const bool bCut = DialoguePlayers[Outgoing]->IsPlaying();

    if (bCut)
        DialoguePlayers[Outgoing]->FadeOut(DialogueFadeSeconds, 0.f);
    DialogueSlot[Fighter] ^= 1;
    auto* Next = DialoguePlayers[Fighter * 2 + DialogueSlot[Fighter]].Get();
    VoicePlayers[Fighter]->Stop();
    Next->Stop();
    Next->SetSound(Sound);
    Next->Play();
    UE_LOG(LogTemp, Display, TEXT("FIGHTER_DIALOGUE_PLAY event=%s player=%d trigger=%u playback=%u faded_previous=%d"),
           *Event, Fighter, Frame, Frame, bCut ? 1 : 0);
}

void UFighterAudioComponent::SetRoundScore(int32 P1, int32 P2, int32 Target)
{
    const bool Next = UsesClimax(P1, P2, Target);

    if (Next == bClimax)
        return;
    bClimax = Next;

    if (!MusicKey.StartsWith(TEXT("menu:")))
        PlayBattleMusic();
    UE_LOG(LogTemp, Display, TEXT("FIGHTER_AUDIO round_score=%d:%d target=%d music=%s"), P1, P2, Target,
           bClimax ? TEXT("climax") : TEXT("normal"));
}

void UFighterAudioComponent::PlayMusicKey(const FString& Key)
{
    if (Key == MusicKey)
        return;
    MusicKey = Key;

    if (!Music)
        return;

    if (FParse::Param(FCommandLine::Get(), TEXT("FighterMuteMusic")))
        Music->SetVolumeMultiplier(.002f);
    const FString Sound = Key.StartsWith(TEXT("menu:")) ? Key.Mid(5) : Key;
    USoundBase* const* Found = MusicByKey.Find(Sound);
    Music->Stop();

    if (Found && *Found)
    {
        Music->SetSound(*Found);
        Music->Play();
    }

    UE_LOG(LogTemp, Display, TEXT("FIGHTER_AUDIO music=%s found=%d"), *Sound, Found ? 1 : 0);
}

void UFighterAudioComponent::SetStageMusic(const FString& StageKey)
{
    StageMusic = StageKey.IsEmpty() ? TEXT("yakushima") : StageKey;

    if (!MusicKey.StartsWith(TEXT("menu:")))
        PlayBattleMusic();
}

void UFighterAudioComponent::PlayMenuMusic(const FString& Key)
{
    PlayMusicKey(TEXT("menu:") + Key);
}

bool UFighterAudioComponent::IsMusicPlaying() const
{
    return Music && Music->IsPlaying();
}

void UFighterAudioComponent::PlayBattleMusic()
{
    FString Key = StageMusic + (bClimax ? TEXT("_climax") : TEXT("_normal"));

    if (Music && !MusicByKey.Contains(Key))
        Key = bClimax ? TEXT("yakushima_climax") : TEXT("yakushima_normal");
    PlayMusicKey(Key);
}

void UFighterAudioComponent::ResetPresentation()
{
    EmittedVoices.Reset();

    for (auto& C : VoicePlayers)
        if (C)
            C->Stop();
    EmittedCinematics.Reset();

    for (auto& C : DialoguePlayers)
        if (C)
            C->Stop();
}

void UFighterAudioComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (Music)
        Music->Stop();

    for (auto& C : VoicePlayers)
        if (C)
            C->Stop();

    for (auto& C : DialoguePlayers)
        if (C)
            C->Stop();
    Super::EndPlay(Reason);
}
