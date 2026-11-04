// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// 입력 매핑 컨텍스트와 우선순위 쌍.
#pragma once

#include "CoreMinimal.h"
#include "FighterMappableConfigPair.generated.h"

class UInputMappingContext;

USTRUCT(BlueprintType)

struct FFighterMappableConfigPair
{
    GENERATED_BODY()
public:
    FFighterMappableConfigPair() = default;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSoftObjectPtr<UInputMappingContext> Config;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 Priority = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    bool bShouldActivateAutomatically = true;
};
