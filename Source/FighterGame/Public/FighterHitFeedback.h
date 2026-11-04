// 피격 시 화면과 체력바의 반응. 전투 판정과 롤백에는 관여하지 않는다.
#pragma once
#include <algorithm>

namespace FighterHitFeedback
{
enum class Kind
{
    Guard,
    Light,
    Heavy,
    Signature
};

struct Profile
{
    float Frames, CameraCm, FovDegrees, Blur, BarPixels;
};

inline Profile MakeProfile(Kind kind, bool airHit)
{
    Profile p;

    switch (kind)
    {
    case Kind::Guard:
        p = {5, .35f, 0, 0, 0};
        break;
    case Kind::Light:
        p = {7, 1.f, .3f, .006f, 2.5f};
        break;
    case Kind::Heavy:
        p = {9, 2.1f, .6f, .010f, 4.5f};
        break;
    default:
        p = {11, 2.8f, .85f, .014f, 6.f};
        break;
    }

    if (airHit)
    {
        p.CameraCm *= .65f;
        p.FovDegrees *= .65f;
        p.Blur *= .65f;
    }

    return p;
}

inline float Envelope(float age, float duration)
{
    const float remaining = 1.f - std::clamp(age / duration, 0.f, 1.f);

    return remaining * remaining;
}

inline float Wave(float age, float duration)
{
    constexpr float keys[]{1.f, -.62f, .36f, -.18f, 0.f};
    const float phase = std::clamp(age / duration, 0.f, 1.f) * 4.f;
    const int index = std::min(3, static_cast<int>(phase));

    return keys[index] + (keys[index + 1] - keys[index]) * (phase - index);
}

struct Pulse
{
    Profile Settings{1, 0, 0, 0, 0};
    float Age = 1000, Direction = 1;

    bool Active() const
    {
        return Age < Settings.Frames;
    }

    void Reset()
    {
        Age = 1000;
    }

    void Start(Profile settings, float direction)
    {
        Settings = settings;
        Direction = direction < 0 ? -1.f : 1.f;
        Age = 0;
    }

    void Advance(float frames)
    {
        Age = std::min(1000.f, Age + std::max(0.f, frames));
    }

    float Kick() const
    {
        return Active() ? Wave(Age, Settings.Frames) : 0.f;
    }

    float Fade() const
    {
        return Active() ? Envelope(Age, Settings.Frames) : 0.f;
    }
};

struct HealthTrail
{
    bool Initialized = false;
    float Current = 0, StartHp = 0, Blend = 0;
    static constexpr float DecayFrames = 12;

    float Displayed() const
    {
        return Current + (StartHp - Current) * Blend;
    }

    void Reset(float hp)
    {
        Initialized = true;
        Current = StartHp = hp;
        Blend = 0;
    }

    void Update(float hp, float elapsedFrames)
    {
        if (!Initialized || hp > Current)
        {
            Reset(hp);

            return;
        }

        if (hp < Current)
        {
            StartHp = Displayed();
            Current = hp;
            Blend = 1;

            return;
        }

        Blend = std::max(0.f, Blend - std::max(0.f, elapsedFrames) / DecayFrames);
    }
};
}
