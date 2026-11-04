// 클라이언트 온라인 세션.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "modular_fighter/match_protocol.hpp"
#include "modular_fighter/rollback.hpp"
#include <deque>
#include <optional>
#include <vector>
#include "FighterNetSession.generated.h"

enum class EFighterLobbyAction : uint8
{
    None,
    SideLeft,
    SideRight,
    PickKazuya,
    PickPaul,
    Ready,
    Unready,
    Rematch,
    Quit,
    Requeue,
    StageYakushima,
    StageUrban,
    MenuPractice,
    MenuMultiplayer,
    MenuQuit,
    MenuStageUrban,
    MenuStageYakushima,
    MenuCancelSearch,
    PauseResume,
    PauseMatching,
    PauseMainMenu,
    PauseQuit,
    PauseReselect,
    ResultPractice
};

UCLASS(ClassGroup = Fighter)

class FIGHTERGAME_API UFighterNetSession : public UActorComponent
{
    GENERATED_BODY()
public:
    UFighterNetSession();

    bool Start();
    bool StartMatching();
    void StopMatching();

    bool IsPracticeMatching() const
    {
        return bPracticeMatching;
    }

    void PromoteMatchingToLobby()
    {
        bPracticeMatching = false;
    }

    void LeaveOnline();

    bool IsOnline() const
    {
        return bOnline;
    }

    bool IsConnected() const
    {
        return Socket != nullptr && bConnected;
    }

    const FString& GetStatus() const
    {
        return Status;
    }

    bool HasRoom() const
    {
        return Room.has_value();
    }

    const modular_fighter::net::RoomState& GetRoom() const
    {
        return *Room;
    }

    const std::optional<modular_fighter::net::MatchResult>& GetResult() const
    {
        return Result;
    }

    modular_fighter::net::Side GetLocalSide() const
    {
        return Room ? Room->slots[Room->your_slot].side : modular_fighter::net::Side::None;
    }

    void SelectSide(modular_fighter::net::Side Side)
    {
        ViewSide = Side;
        Send(modular_fighter::net::SelectSide{Side});
    }

    modular_fighter::net::Side GetViewSide() const
    {
        return ViewSide;
    }

    bool WantsViewFlip(bool bLocalRight) const
    {
        return bLocalRight != (ViewSide == modular_fighter::net::Side::Right);
    }

    void SelectCharacter(modular_fighter::CharacterId Character)
    {
        Send(modular_fighter::net::SelectCharacter{Character});
    }

    void Ready()
    {
        Send(modular_fighter::net::Ready{});
    }

    void SelectStage(uint8 Stage)
    {
        Send(modular_fighter::net::SelectStage{Stage});
    }

    void Rematch(bool bAccept)
    {
        Send(modular_fighter::net::Rematch{bAccept});
    }

    void Requeue();

    std::optional<modular_fighter::net::LoadStage> TakeLoadStage();
    void ReportStageLoaded(uint32 MatchId);

    std::optional<modular_fighter::net::MatchStart> TakeMatchStart();

    bool InMatch() const
    {
        return Session.has_value();
    }

    const modular_fighter::State& GetState() const
    {
        return Session->GetState();
    }

    uint8 GetLocalPlayer() const
    {
        return LocalPlayer;
    }

    const std::optional<modular_fighter::net::MatchStart>& GetRound() const
    {
        return Match;
    }

    const modular_fighter::State* GetConfirmedState() const
    {
        return Session ? &Session->ConfirmedState() : nullptr;
    }

    uint32 GetRoundFrameLimit() const
    {
        return Match ? modular_fighter::net::RoundFrameLimit(Match->round, MatchFrames) : MatchFrames;
    }

    modular_fighter::AdvanceResult AdvanceLocal(const modular_fighter::Input& Local);

    void ReportIfFinished();
    const modular_fighter::State* GetConfirmedStateAt(uint32 Frame) const;

    TFunction<TOptional<modular_fighter::Input>(uint32 InputFrame)> LocalInputSource;

    uint64 GetRollbacks() const
    {
        return Session ? Session->RollbackCount() : 0;
    }

    uint32 GetStalls() const
    {
        return Stalls + SyncWaits;
    }

    void Pump();
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    void Send(const modular_fighter::net::Message& Message);
    void Handle(const modular_fighter::net::Message& Message);
    void Close(const TCHAR* Why);
    bool Connect(const FString& Address);

    bool bOnline = false;
    modular_fighter::net::Side ViewSide = modular_fighter::net::Side::None;
    bool bPracticeMatching = false;
    bool bConnected = false;
    FString Status;
    class FSocket* Socket = nullptr;
    modular_fighter::net::FrameReader Reader;
    std::vector<uint8> Outbox;
    std::optional<modular_fighter::net::RoomState> Room;
    std::optional<modular_fighter::net::MatchResult> Result;
    std::optional<modular_fighter::net::LoadStage> PendingLoad;
    std::optional<modular_fighter::net::MatchStart> PendingStart;
    std::optional<modular_fighter::net::MatchStart> Match;
    std::optional<modular_fighter::RollbackSession> Session;
    std::vector<modular_fighter::Input> LocalInputs;
    uint8 LocalPlayer = 0;
    uint32 Stalls = 0;
    bool bReported = false;
    std::optional<modular_fighter::State> EndState;
    uint32 NextCheck = 0;
    int64 RemoteNewest = -1;
    static constexpr int64 MaxFramesAhead = 3;
    uint32 SyncWaits = 0;
    uint32 MatchFrames = modular_fighter::net::DefaultMatchFrames;
};
