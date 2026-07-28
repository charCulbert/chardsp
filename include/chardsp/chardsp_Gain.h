#pragma once

#include "chardsp/chardsp_OpenSourceLicenses.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace chardsp
{

template <typename SampleType>
inline SampleType gain (SampleType sample, SampleType amount) noexcept
{
    static_assert (std::is_floating_point_v<SampleType>);
    return sample * amount;
}

template <typename SampleType>
inline SampleType linearCrossfade (SampleType first,
                                   SampleType second,
                                   SampleType position) noexcept
{
    static_assert (std::is_floating_point_v<SampleType>);
    const auto mix = std::isfinite (position)
        ? std::clamp (position, SampleType {}, static_cast<SampleType> (1))
        : SampleType {};
    return first + (second - first) * mix;
}

template <typename SampleType>
inline SampleType equalPowerCrossfade (SampleType first,
                                       SampleType second,
                                       SampleType position) noexcept
{
    static_assert (std::is_floating_point_v<SampleType>);
    const auto mix = std::isfinite (position)
        ? std::clamp (position, SampleType {}, static_cast<SampleType> (1))
        : SampleType {};

    if (mix == SampleType {})
        return first;

    if (mix == static_cast<SampleType> (1))
        return second;

    constexpr auto halfPi = static_cast<SampleType> (
        1.57079632679489661923);
    return first * std::cos (mix * halfPi)
         + second * std::sin (mix * halfPi);
}

} // namespace chardsp
