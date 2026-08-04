#pragma once

#include "chardsp/chardsp_OpenSourceLicenses.h"

#include <algorithm>
#include <cstdint>
#include <type_traits>

namespace chardsp
{

enum class NoiseColor
{
    brown,
    pink,
    white,
    blue
};

template <typename SampleType>
struct ColoredNoise
{
    static_assert (std::is_same_v<SampleType, float>
                   || std::is_same_v<SampleType, double>,
                   "ColoredNoise supports float or double");

    void reset (uint32_t newSeed = 1) noexcept
    {
        seed = newSeed;
        brownState = SampleType {};
        pinkState0 = SampleType {};
        pinkState1 = SampleType {};
        pinkState2 = SampleType {};
        previousWhite = SampleType {};
    }

    void setColor (NoiseColor nextColor) noexcept
    {
        color = nextColor;
    }

    SampleType next() noexcept
    {
        const auto white = randomBipolar();
        brownState += static_cast<SampleType> (0.018) * white
                    - static_cast<SampleType> (0.0012) * brownState;
        brownState = std::clamp (brownState,
                                 static_cast<SampleType> (-1.25),
                                 static_cast<SampleType> (1.25));
        pinkState0 = static_cast<SampleType> (0.99765) * pinkState0
                   + static_cast<SampleType> (0.0990460) * white;
        pinkState1 = static_cast<SampleType> (0.96300) * pinkState1
                   + static_cast<SampleType> (0.2965164) * white;
        pinkState2 = static_cast<SampleType> (0.57000) * pinkState2
                   + static_cast<SampleType> (1.0526913) * white;

        const auto brown = std::clamp (brownState * static_cast<SampleType> (0.95),
                                       static_cast<SampleType> (-1.0),
                                       static_cast<SampleType> (1.0));
        const auto pink = std::clamp ((pinkState0 + pinkState1 + pinkState2
                                       + static_cast<SampleType> (0.1848) * white)
                                          * static_cast<SampleType> (0.21),
                                      static_cast<SampleType> (-1.0),
                                      static_cast<SampleType> (1.0));
        const auto blue = std::clamp ((white - previousWhite)
                                          * static_cast<SampleType> (0.95),
                                      static_cast<SampleType> (-1.0),
                                      static_cast<SampleType> (1.0));
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
    SampleType randomBipolar() noexcept
    {
        seed = uint32_t { 1664525 } * seed + uint32_t { 1013904223 };
        const auto signedSeed = seed <= uint32_t { 0x7fffffff }
            ? static_cast<int64_t> (seed)
            : static_cast<int64_t> (seed) - int64_t { 0x100000000 };
        return static_cast<SampleType> (4.656613e-10)
             * static_cast<SampleType> (signedSeed);
    }

    uint32_t seed = 1;
    SampleType brownState {};
    SampleType pinkState0 {};
    SampleType pinkState1 {};
    SampleType pinkState2 {};
    SampleType previousWhite {};
    NoiseColor color = NoiseColor::white;
};

} // namespace chardsp
