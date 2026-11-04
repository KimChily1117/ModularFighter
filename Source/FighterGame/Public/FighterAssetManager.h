// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// 프로젝트 공용 에셋 로딩.
#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetManager.h"
#include "FighterAssetManager.generated.h"

UCLASS()

class FIGHTERGAME_API UFighterAssetManager : public UAssetManager
{
    GENERATED_BODY()

public:
    UFighterAssetManager();

    static UFighterAssetManager& Get();

    virtual void StartInitialLoading() final;

    static bool ShouldLogAssetLoads();

    static UObject* SynchronousLoadAsset(const FSoftObjectPath& AssetPath);

    template <typename AssetType>
    static AssetType* GetAsset(const TSoftObjectPtr<AssetType>& AssetPointer, bool bKeepInMemory = true);

    template <typename AssetType>
    static TSubclassOf<AssetType> GetSubclass(const TSoftClassPtr<AssetType>& AssetPointer, bool bKeepInMemory = true);

    void AddLoadedAsset(const UObject* Asset);

    UPROPERTY()
    TSet<TObjectPtr<const UObject>> LoadedAssets;

    FCriticalSection SyncObject;
};

template <typename AssetType>
AssetType* UFighterAssetManager::GetAsset(const TSoftObjectPtr<AssetType>& AssetPointer, bool bKeepsInMemory)
{
    AssetType* LoadedAsset = nullptr;
    const FSoftObjectPath& AssetPath = AssetPointer.ToSoftObjectPath();

    if (AssetPath.IsValid())
    {
        LoadedAsset = AssetPointer.Get();

        if (!LoadedAsset)
        {
            LoadedAsset = Cast<AssetType>(SynchronousLoadAsset(AssetPath));
            ensureAlwaysMsgf(LoadedAsset, TEXT("Failed to load asset [%s]"), *AssetPointer.ToString());
        }

        if (LoadedAsset && bKeepsInMemory)
        {
            Get().AddLoadedAsset(Cast<UObject>(LoadedAsset));
        }
    }

    return LoadedAsset;
}

template <typename AssetType>
TSubclassOf<AssetType> UFighterAssetManager::GetSubclass(const TSoftClassPtr<AssetType>& AssetPointer,
                                                         bool bKeepInMemory)
{
    TSubclassOf<AssetType> LoadedSubclass;

    const FSoftObjectPath& AssetPath = AssetPointer.ToSoftObjectPath();

    if (AssetPath.IsValid())
    {
        LoadedSubclass = AssetPointer.Get();

        if (!LoadedSubclass)
        {
            LoadedSubclass = Cast<UClass>(SynchronousLoadAsset(AssetPath));
            ensureAlwaysMsgf(LoadedSubclass, TEXT("Failed to load asset class [%s]"), *AssetPointer.ToString());
        }

        if (LoadedSubclass && bKeepInMemory)
        {
            Get().AddLoadedAsset(Cast<UObject>(LoadedSubclass));
        }
    }

    return LoadedSubclass;
}
