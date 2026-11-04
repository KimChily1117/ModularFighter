// 타격 강도에 따라 이펙트 단계를 고른다. 체력 피해와는 분리한다.
#pragma once
#include "FighterT6ImpactPolicy.h"

namespace FighterImpactPolicy
{
enum class Tier
{
    Guard,
    Light,
    Heavy,
    Signature
};

struct Plan
{
    Tier Level;
    float Strength;
    float ContactScale;
    float StruckScale;
    bool Weld;
    bool CharacterMesh;
    unsigned char T6StrengthPair = 0;
};

inline Plan Select(float SourcePower, bool Guard, bool Counter, bool Signature)
{
    if (Guard)
        return {Tier::Guard, .55f, .85f, 0.f, false, false};
    const auto Pair = FighterT6ImpactPolicy::FromMoveBasePower(static_cast<int>(SourcePower)).StrengthPair;

    if (Signature)
        return {Tier::Signature, 1.35f, 1.12f, 1.f, true, true, Pair};

    if (Counter || Pair)
        return {Tier::Heavy, 1.1f, 1.f, .85f, true, false, Pair};

    return {Tier::Light, .65f, .82f, .55f, false, false, Pair};
}

inline const char* Name(Tier Level)
{
    switch (Level)
    {
    case Tier::Guard:
        return "guard";
    case Tier::Light:
        return "light";
    case Tier::Heavy:
        return "heavy";
    default:
        return "signature";
    }
}
}
