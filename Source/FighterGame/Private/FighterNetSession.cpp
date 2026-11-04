// TCP 연결, 메시지 처리, 온라인 롤백 진행.
#include "FighterNetSession.h"

#include "Common/TcpSocketBuilder.h"
#include "IPAddress.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogFighterNet, Log, All);

namespace Core = modular_fighter;
namespace Net = modular_fighter::net;

UFighterNetSession::UFighterNetSession()
{
    PrimaryComponentTick.bCanEverTick = false;
}

bool UFighterNetSession::Start()
{
    FString Address;

    if (!FParse::Value(FCommandLine::Get(), TEXT("FighterServer="), Address))
        return false;

    return Connect(Address);
}

bool UFighterNetSession::StartMatching()
{
    if (bOnline)
        return true;
    FString Address = TEXT("127.0.0.1:7777");
    FParse::Value(FCommandLine::Get(), TEXT("FighterMatchServer="), Address);
    bPracticeMatching = true;

    return Connect(Address);
}

void UFighterNetSession::StopMatching()
{
    if (!bOnline || Room)
        return;
    Close(TEXT("MATCHING OFF"));
    bOnline = false;
    bPracticeMatching = false;
    Status.Reset();
}

void UFighterNetSession::LeaveOnline()
{
    if (Socket)
        Close(TEXT("LEFT"));
    Room.reset();
    Result.reset();
    ViewSide = Net::Side::None;
    PendingLoad.reset();
    PendingStart.reset();
    Match.reset();
    Session.reset();
    EndState.reset();
    Reader = modular_fighter::net::FrameReader{};
    Outbox.clear();
    LocalInputs.clear();
    bReported = false;
    Stalls = 0;
    bOnline = false;
    bPracticeMatching = false;
    Status.Reset();
}

// 매치 서버에 연결한다.
bool UFighterNetSession::Connect(const FString& Address)
{
    bOnline = true;
    FParse::Value(FCommandLine::Get(), TEXT("FighterMatchFrames="), MatchFrames);
    FString Host = Address, PortText = TEXT("7777");
    Address.Split(TEXT(":"), &Host, &PortText);
    ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    TSharedRef<FInternetAddr> Remote = Sockets->CreateInternetAddr();
    bool bValid = false;
    Remote->SetIp(*Host, bValid);
    Remote->SetPort(FCString::Atoi(*PortText));

    if (!bValid)
    {
        Status = TEXT("INVALID SERVER ADDRESS");

        return true;
    }

    Socket = FTcpSocketBuilder(TEXT("FighterMatch")).AsReusable().Build();

    if (!Socket || !Socket->Connect(*Remote))
    {
        Close(TEXT("CONNECT FAILED"));

        return true;
    }

    Socket->SetNonBlocking(true);
    Socket->SetNoDelay(true);
    bConnected = true;
    Status = TEXT("SEARCHING FOR OPPONENT");
    Send(Net::Hello{Net::ProtocolVersion, Core::RulesFingerprint()});
    Send(Net::QueueMatch{});
    UE_LOG(LogFighterNet, Display, TEXT("FIGHTER_NET connected %s fingerprint=%016llx"), *Address,
           Core::RulesFingerprint());

    return true;
}

void UFighterNetSession::Send(const Net::Message& Message)
{
    if (!IsConnected())
        return;
    const auto Bytes = Net::Encode(Message);
    Outbox.insert(Outbox.end(), Bytes.begin(), Bytes.end());
}

// 소켓의 메시지를 읽어 처리한다.
void UFighterNetSession::Pump()
{
    if (!IsConnected())
        return;
    uint8 Buffer[4096];
    uint32 Pending = 0;

    while (Socket->HasPendingData(Pending))
    {
        int32 Read = 0;

        if (!Socket->Recv(Buffer, sizeof Buffer, Read) || Read <= 0)
        {
            Close(TEXT("SERVER CLOSED"));

            return;
        }

        Reader.Append(Buffer, static_cast<std::size_t>(Read));
    }

    if (Socket->GetConnectionState() == SCS_ConnectionError)
    {
        Close(TEXT("CONNECTION LOST"));

        return;
    }
    while (auto Message = Reader.Next())
        Handle(*Message);

    if (Reader.Failed())
    {
        Close(TEXT("BAD DATA FROM SERVER"));

        return;
    }
    while (!Outbox.empty())
    {
        int32 Sent = 0;

        if (!Socket->Send(Outbox.data(), static_cast<int32>(Outbox.size()), Sent))
        {
            Close(TEXT("SEND FAILED"));

            return;
        }

        if (Sent <= 0)
            break;
        Outbox.erase(Outbox.begin(), Outbox.begin() + Sent);
    }
}

// 서버 메시지를 처리한다.
void UFighterNetSession::Handle(const Net::Message& Message)
{
    if (const auto* State = std::get_if<Net::RoomState>(&Message))
    {
        Room = *State;
        Status.Reset();

        if (State->phase == Net::Phase::Closed && !Result)
            Requeue();
    }
    else if (const auto* Load = std::get_if<Net::LoadStage>(&Message))
    {
        PendingLoad = *Load;
        Result.reset();
        UE_LOG(LogFighterNet, Display, TEXT("FIGHTER_NET match %u load stage %u"), Load->match_id, Load->stage);
    }
    else if (const auto* Go = std::get_if<Net::MatchStart>(&Message))
    {
        PendingStart = *Go;
        Match = *Go;
        Result.reset();
        LocalPlayer = Go->your_side == Net::Side::Left ? 0 : 1;
        Session.emplace(LocalPlayer, 120, Go->left, Go->right);
        LocalInputs.assign(Go->input_delay, Core::Input{});
        Stalls = 0;
        RemoteNewest = -1;
        SyncWaits = 0;
        bReported = false;
        EndState.reset();
        NextCheck = 0;
        UE_LOG(LogFighterNet, Display, TEXT("FIGHTER_NET match %u start: core player %u delay %u"), Go->match_id,
               LocalPlayer, Go->input_delay);
    }
    else if (const auto* Batch = std::get_if<Net::InputBatch>(&Message))
    {
        if (!Session)
            return;

        if (!Batch->inputs.empty())
            RemoteNewest = FMath::Max<int64>(RemoteNewest, int64(Batch->first_frame) + int64(Batch->inputs.size()) - 1);

        for (std::size_t I = 0; I < Batch->inputs.size(); ++I)
        {
            const auto R = Session->ReceiveRemote(Batch->first_frame + static_cast<uint32>(I), Batch->inputs[I]);

            if (R == Core::ReceiveResult::Conflict || R == Core::ReceiveResult::InvalidInput)
            {
                Close(TEXT("REMOTE INPUT CONFLICT"));

                return;
            }
        }
    }
    else if (const auto* Done = std::get_if<Net::MatchResult>(&Message))
    {
        Result = *Done;
        UE_LOG(LogFighterNet, Display, TEXT("FIGHTER_NET match %u result status=%u winner=%u rollbacks=%llu stalls=%u"),
               Done->match_id, uint32(Done->status), Done->winner, GetRollbacks(), Stalls);
    }
    else if (const auto* Failure = std::get_if<Net::Error>(&Message))
    {
        UE_LOG(LogFighterNet, Warning, TEXT("FIGHTER_NET server error %u"), uint32(Failure->code));
    }
}

void UFighterNetSession::Requeue()
{
    Room.reset();
    Result.reset();
    Session.reset();
    ViewSide = Net::Side::None;
    Status = TEXT("SEARCHING FOR OPPONENT");
    Send(Net::QueueMatch{});
}

std::optional<Net::LoadStage> UFighterNetSession::TakeLoadStage()
{
    auto Load = PendingLoad;
    PendingLoad.reset();

    return Load;
}

void UFighterNetSession::ReportStageLoaded(uint32 MatchId)
{
    Send(Net::StageLoaded{MatchId});
    UE_LOG(LogFighterNet, Display, TEXT("FIGHTER_NET match %u stage loaded"), MatchId);
}

std::optional<Net::MatchStart> UFighterNetSession::TakeMatchStart()
{
    auto Start = PendingStart;
    PendingStart.reset();

    return Start;
}

// 로컬 입력을 보내고 시뮬레이션을 진행한다.
Core::AdvanceResult UFighterNetSession::AdvanceLocal(const Core::Input& Local)
{
    if (!Session || Result)
        return Core::AdvanceResult::AwaitingRemote;
    const uint32 Frame = Session->GetState().frame;

    if (Frame >= GetRoundFrameLimit() + Net::RoundEndHoldFrames ||
        (EndState && Frame >= EndState->frame + Net::RoundEndHoldFrames))
        return Core::AdvanceResult::AwaitingRemote;

    const int64 RemoteFrame = RemoteNewest - Match->input_delay;

    if (int64(Frame) - RemoteFrame > MaxFramesAhead)
    {
        ++SyncWaits;

        return Core::AdvanceResult::AwaitingRemote;
    }

    const uint8 Delay = Match->input_delay;

    if (LocalInputs.size() == static_cast<std::size_t>(Frame) + Delay)
    {
        Core::Input Decided = Local;

        if (LocalInputSource)
        {
            const TOptional<Core::Input> FromSource = LocalInputSource(Frame + Delay);

            if (!FromSource)
            {
                ++SyncWaits;

                return Core::AdvanceResult::AwaitingRemote;
            }

            Decided = *FromSource;
        }

        LocalInputs.push_back(Net::IntroInput(Decided, Frame + Delay, Match->round));
    }

    const auto R = Session->Advance(LocalInputs[Frame]);

    if (R == Core::AdvanceResult::AwaitingRemote)
        ++Stalls;
    Net::InputBatch Batch;
    const uint32 Newest = static_cast<uint32>(LocalInputs.size() - 1);
    Batch.first_frame = Newest >= Net::MaxBatchInputs - 1 ? Newest - static_cast<uint32>(Net::MaxBatchInputs - 1) : 0;
    Batch.ack_frame = Session->ConfirmedFrame();
    Batch.inputs.assign(LocalInputs.begin() + Batch.first_frame, LocalInputs.end());
    Send(Batch);

    return R;
}

const Core::State* UFighterNetSession::GetConfirmedStateAt(uint32 Frame) const
{
    return Session ? Session->ConfirmedStateAt(Frame) : nullptr;
}

// 경기 결과와 해시를 서버에 보고한다.
void UFighterNetSession::ReportIfFinished()
{
    if (!Session || bReported)
        return;

    if (!EndState)
        if (const Core::State* Finished = Net::FirstFinishedFrame(*Session, NextCheck, GetRoundFrameLimit()))
            EndState = *Finished;

    if (!EndState || Session->ConfirmedFrame() < EndState->frame + Net::RoundEndHoldFrames)
        return;
    const Core::State& End = *EndState;
    const uint8 Winner = Net::MatchWinner(End);
    Send(Net::ResultReport{End.frame, Winner, Core::HashState(End)});
    bReported = true;
    UE_LOG(LogFighterNet, Display, TEXT("FIGHTER_NET report frame=%u winner=%u hash=%016llx"), End.frame, Winner,
           Core::HashState(End));
}

void UFighterNetSession::Close(const TCHAR* Why)
{
    if (Socket)
    {
        Socket->Close();
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
        Socket = nullptr;
    }

    bConnected = false;
    Status = Why;
    Session.reset();
    UE_LOG(LogFighterNet, Warning, TEXT("FIGHTER_NET closed: %s"), Why);
}

void UFighterNetSession::EndPlay(const EEndPlayReason::Type Reason)
{
    if (Socket)
        Close(TEXT("ENDED"));
    Super::EndPlay(Reason);
}
