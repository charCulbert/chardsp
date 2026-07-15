#pragma once

#include <algorithm>
#include <cstdint>

namespace chardsp
{

enum class NoiseColor
{
    brown,
    pink,
    white,
    blue
};

struct ColoredNoise
{
    void reset (uint32_t newSeed = 1) noexcept
    {
        seed = newSeed;
        brownState = 0.0f;
        pinkState0 = 0.0f;
        pinkState1 = 0.0f;
        pinkState2 = 0.0f;
        previousWhite = 0.0f;
    }

    void setColor (NoiseColor nextColor) noexcept
    {
        color = nextColor;
    }

    float next() noexcept
    {
        const auto white = randomBipolar();
        brownState += 0.018f * white - 0.0012f * brownState;
        brownState = std::clamp (brownState, -1.25f, 1.25f);
        pinkState0 = 0.99765f * pinkState0 + 0.0990460f * white;
        pinkState1 = 0.96300f * pinkState1 + 0.2965164f * white;
        pinkState2 = 0.57000f * pinkState2 + 1.0526913f * white;

        const auto brown = std::clamp (brownState * 0.95f, -1.0f, 1.0f);
        const auto pink = std::clamp ((pinkState0 + pinkState1 + pinkState2
                                       + 0.1848f * white) * 0.21f,
                                      -1.0f,
                                      1.0f);
        const auto blue = std::clamp ((white - previousWhite) * 0.95f,
                                      -1.0f,
                                      1.0f);
        previousWhite = white;

        switch (color)
        {
            case NoiseColor::brown: return brown;
            case NoiseColor::pink:  return pink;
            case NoiseColor::white: return white;
            case NoiseColor::blue:  return blue;
        }

        return white;
    }

private:
    float randomBipolar() noexcept
    {
        seed = uint32_t { 1664525 } * seed + uint32_t { 1013904223 };
        const auto signedSeed = seed <= uint32_t { 0x7fffffff }
            ? static_cast<int64_t> (seed)
            : static_cast<int64_t> (seed) - int64_t { 0x100000000 };
        return 4.656613e-10f * static_cast<float> (signedSeed);
    }

    uint32_t seed = 1;
    float brownState = 0.0f;
    float pinkState0 = 0.0f;
    float pinkState1 = 0.0f;
    float pinkState2 = 0.0f;
    float previousWhite = 0.0f;
    NoiseColor color = NoiseColor::white;
};

} // namespace chardsp
