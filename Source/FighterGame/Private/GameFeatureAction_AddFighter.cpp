// Game Feature로 캐릭터를 등록하고 해제한다.
#include "GameFeatureAction_AddFighter.h"
#include "FighterCharacterRegistry.h"
#include "FighterPawnData.h"
#include "GameFeatureData.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GameFeatureAction_AddFighter)

FString UGameFeatureAction_AddFighter::SourceName() const
{
    FString PluginName;

    if (const auto* Data = GetTypedOuter<UGameFeatureData>())
        Data->GetPluginName(PluginName);

    return PluginName.IsEmpty() ? GetNameSafe(GetOuter()) : PluginName;
}

void UGameFeatureAction_AddFighter::OnGameFeatureActivating(FGameFeatureActivatingContext& Context)
{
    Super::OnGameFeatureActivating(Context);

    if (UFighterCharacterRegistry* Registry = UFighterCharacterRegistry::Get())
        for (const TSoftObjectPtr<UFighterPawnData>& Fighter : Fighters)
            Registry->RegisterFighter(Fighter.LoadSynchronous(), SourceName());
}

void UGameFeatureAction_AddFighter::OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context)
{
    Super::OnGameFeatureDeactivating(Context);

    if (UFighterCharacterRegistry* Registry = UFighterCharacterRegistry::Get())
        for (const TSoftObjectPtr<UFighterPawnData>& Fighter : Fighters)
            Registry->UnregisterFighter(Fighter.Get(), SourceName());
}
