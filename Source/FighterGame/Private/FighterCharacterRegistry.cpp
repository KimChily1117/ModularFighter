// Game Feature가 등록한 캐릭터 조회.
#include "FighterCharacterRegistry.h"
#include "FighterPawnData.h"
#include "Engine/Engine.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(FighterCharacterRegistry)

DEFINE_LOG_CATEGORY_STATIC(LogFighterCharacter, Log, All);

UFighterCharacterRegistry* UFighterCharacterRegistry::Get()
{
    return GEngine ? GEngine->GetEngineSubsystem<UFighterCharacterRegistry>() : nullptr;
}

bool UFighterCharacterRegistry::RegisterFighter(UFighterPawnData* PawnData, const FString& Source)
{
    if (!PawnData)
    {
        UE_LOG(LogFighterCharacter, Error, TEXT("FIGHTER_CHARACTER refused from %s: pawn data did not load"), *Source);

        return false;
    }

    const FString Compiled = PawnData->GetCompiledModuleHash();

    if (PawnData->ModuleDataHash != Compiled)
    {
        UE_LOG(LogFighterCharacter, Error,
               TEXT("FIGHTER_CHARACTER refused %s from %s: authored for module data %s, CombatCore has %s"),
               *PawnData->GetPathName(), *Source, *PawnData->ModuleDataHash, *Compiled);

        return false;
    }

    Registered.Add(FSoftObjectPath(PawnData), PawnData);
    UE_LOG(LogFighterCharacter, Display, TEXT("FIGHTER_CHARACTER registered %s from %s (module %s)"),
           *PawnData->CharacterSlug, *Source, *Compiled);

    return true;
}

void UFighterCharacterRegistry::UnregisterFighter(const UFighterPawnData* PawnData, const FString& Source)
{
    if (PawnData && Registered.Remove(FSoftObjectPath(PawnData)) > 0)
        UE_LOG(LogFighterCharacter, Display, TEXT("FIGHTER_CHARACTER unregistered %s (%s)"), *PawnData->CharacterSlug,
               *Source);
}

UFighterPawnData* UFighterCharacterRegistry::FindAvailable(const FSoftObjectPath& Path) const
{
    const TObjectPtr<UFighterPawnData>* Found = Registered.Find(Path);

    return Found ? Found->Get() : nullptr;
}
