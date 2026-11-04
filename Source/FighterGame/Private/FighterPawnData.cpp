// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// Pawn Data 구현.
#include "FighterPawnData.h"
#include "FighterInputConfig.h"
#include "FighterAbilitySet.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(FighterPawnData)

UFighterPawnData::UFighterPawnData(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {}

FString UFighterPawnData::GetCompiledModuleHash() const
{
    return FString::Printf(TEXT("%016llx"),
                           static_cast<unsigned long long>(modular_fighter::ModuleDataHash(GetCoreCharacter())));
}
