// 활성화된 캐릭터 데이터 목록.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "FighterCharacterRegistry.generated.h"

class UFighterPawnData;

UCLASS()

class FIGHTERGAME_API UFighterCharacterRegistry : public UEngineSubsystem
{
    GENERATED_BODY()
public:
    static UFighterCharacterRegistry* Get();

    bool RegisterFighter(UFighterPawnData* PawnData, const FString& Source);
    void UnregisterFighter(const UFighterPawnData* PawnData, const FString& Source);

    UFighterPawnData* FindAvailable(const FSoftObjectPath& Path) const;

    int32 GetNumRegistered() const
    {
        return Registered.Num();
    }

private:
    UPROPERTY(Transient)
    TMap<FSoftObjectPath, TObjectPtr<UFighterPawnData>> Registered;
};
