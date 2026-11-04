// 애니메이션 좌표계와 언리얼 좌표계의 변환.
#pragma once

#include "CoreMinimal.h"

namespace FighterMotionCoordinates
{
inline FVector Position(const FVector& Raw)
{
    return FVector(Raw.Z, -Raw.X, Raw.Y);
}

inline FQuat Rotation(const FQuat& Raw)
{
    return FQuat(-Raw.Z, Raw.X, -Raw.Y, Raw.W);
}

inline FVector Scale(const FVector& Raw)
{
    return FVector(Raw.Z, Raw.X, Raw.Y);
}
}
