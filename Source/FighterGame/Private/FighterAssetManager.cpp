// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// 에셋 관리자 구현.
#include "FighterAssetManager.h"

UFighterAssetManager::UFighterAssetManager() : UAssetManager() {}

UFighterAssetManager& UFighterAssetManager::Get()
{
    check(GEngine);

    if (UFighterAssetManager* Singleton = Cast<UFighterAssetManager>(GEngine->AssetManager))
    {
        return *Singleton;
    }

    UE_LOG(
        LogTemp, Fatal,
        TEXT("invalid AssetManagerClassname in DefaultEngine.ini(project settings); it must be FighterAssetManager"));

    return *NewObject<UFighterAssetManager>();
}

void UFighterAssetManager::StartInitialLoading()
{
    Super::StartInitialLoading();
}

bool UFighterAssetManager::ShouldLogAssetLoads()
{
    const TCHAR* CommandLineContent = FCommandLine::Get();
    static bool bLogAssetLoads = FParse::Param(CommandLineContent, TEXT("LogAssetLoads"));

    return bLogAssetLoads;
}

UObject* UFighterAssetManager::SynchronousLoadAsset(const FSoftObjectPath& AssetPath)
{
    if (AssetPath.IsValid())
    {
        TUniquePtr<FScopeLogTime> LogTimePtr;

        if (ShouldLogAssetLoads())
        {
            LogTimePtr = MakeUnique<FScopeLogTime>(
                *FString::Printf(TEXT("synchronous loaded assets [%s]"), *AssetPath.ToString()), nullptr,
                FScopeLogTime::ScopeLog_Seconds);
        }

        if (UAssetManager::IsValid())
        {
            return UAssetManager::GetStreamableManager().LoadSynchronous(AssetPath);
        }

        return AssetPath.TryLoad();
    }

    return nullptr;
}

void UFighterAssetManager::AddLoadedAsset(const UObject* Asset)
{
    if (ensureAlways(Asset))
    {
        FScopeLock Lock(&SyncObject);
        LoadedAssets.Add(Asset);
    }
}
