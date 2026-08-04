#pragma once

#include "chardsp/chardsp_OpenSourceLicenses.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace chardsp
{
namespace detail
{

template <typename SampleType>
inline SampleType detectorCoefficient (SampleType sampleRate,
                                       SampleType seconds) noexcept
{
    if (! std::isfinite (sampleRate) || sampleRate <= SampleType {}
        || ! std::isfinite (seconds) || seconds <= SampleType {})
        return SampleType {};

    return std::exp (static_cast<SampleType> (-1)
                     / (sampleRate * seconds));
}

template <typename SampleType>
inline SampleType finiteMagnitude (SampleType sample) noexcept
{
    return std::isfinite (sample) ? std::abs (sample) : SampleType {};
}

} // namespace detail

// An instantaneous-attack peak follower. releaseSeconds is the time for an
// undriven value to decay to 1/e of its starting level.
template <typename SampleType>
struct PeakFollower
{
    static_assert (std::is_same_v<SampleType, float>
                   || std::is_same_v<SampleType, double>,
                   "PeakFollower supports float or double");

    void prepare (SampleType newSampleRate,
                  SampleType releaseSeconds =
                      static_cast<SampleType> (0.3)) noexcept
    {
        sampleRate = std::isfinite (newSampleRate)
                && newSampleRate > SampleType {}
            ? newSampleRate : static_cast<SampleType> (48000);
        setReleaseTime (releaseSeconds);
        reset();
    }

    void reset() noexcept { value = SampleType {}; }

    void setReleaseTime (SampleType seconds) noexcept
    {
        releaseCoefficient = detail::detectorCoefficient (
            sampleRate, seconds);
    }

    SampleType process (SampleType sample) noexcept
    {
        const auto magnitude = detail::finiteMagnitude (sample);
        value = std::max (magnitude, value * releaseCoefficient);

        if (value < static_cast<SampleType> (1.0e-20))
            value = SampleType {};

        return value;
    }

    SampleType getCurrent() const noexcept { return value; }

private:
    SampleType sampleRate = static_cast<SampleType> (48000);
    SampleType releaseCoefficient = SampleType {};
    SampleType value = SampleType {};
};

// Exponential RMS. timeConstantSeconds is the time for the mean-square state
// to move 1 - 1/e of the way toward a constant input's squared magnitude.
template <typename SampleType>
struct RMSFollower
{
    static_assert (std::is_same_v<SampleType, float>
                   || std::is_same_v<SampleType, double>,
                   "RMSFollower supports float or double");

    void prepare (SampleType newSampleRate,
                  SampleType timeConstantSeconds =
                      static_cast<SampleType> (0.3)) noexcept
    {
        sampleRate = std::isfinite (newSampleRate)
                && newSampleRate > SampleType {}
            ? newSampleRate : static_cast<SampleType> (48000);
        setTimeConstant (timeConstantSeconds);
        reset();
    }

    void reset() noexcept { meanSquare = SampleType {}; }

    void setTimeConstant (SampleType seconds) noexcept
    {
        coefficient = detail::detectorCoefficient (sampleRate, seconds);
    }

    SampleType process (SampleType sample) noexcept
    {
        sample = std::isfinite (sample) ? sample : SampleType {};
        const auto square = sample * sample;
        meanSquare = coefficient * meanSquare
                   + (static_cast<SampleType> (1) - coefficient) * square;

        if (meanSquare < static_cast<SampleType> (1.0e-30))
            meanSquare = SampleType {};

        return std::sqrt (std::max (meanSquare, SampleType {}));
    }

    SampleType getCurrent() const noexcept
    {
        return std::sqrt (std::max (meanSquare, SampleType {}));
    }

private:
    SampleType sampleRate = static_cast<SampleType> (48000);
    SampleType coefficient = SampleType {};
    SampleType meanSquare = SampleType {};
};

// A rectified attack/release envelope. Each time is the corresponding 1/e
// time constant; a zero or invalid time selects an immediate transition.
template <typename SampleType>
struct EnvelopeFollower
{
    static_assert (std::is_same_v<SampleType, float>
                   || std::is_same_v<SampleType, double>,
                   "EnvelopeFollower supports float or double");

    void prepare (SampleType newSampleRate,
                  SampleType attackSeconds =
                      static_cast<SampleType> (0.01),
                  SampleType releaseSeconds =
                      static_cast<SampleType> (0.1)) noexcept
    {
        sampleRate = std::isfinite (newSampleRate)
                && newSampleRate > SampleType {}
            ? newSampleRate : static_cast<SampleType> (48000);
        setAttackAndRelease (attackSeconds, releaseSeconds);
        reset();
    }

    void reset() noexcept { value = SampleType {}; }

    void setAttackAndRelease (SampleType attackSeconds,
                              SampleType releaseSeconds) noexcept
    {
        attackCoefficient = detail::detectorCoefficient (
            sampleRate, attackSeconds);
        releaseCoefficient = detail::detectorCoefficient (
            sampleRate, releaseSeconds);
    }

    SampleType process (SampleType sample) noexcept
    {
        const auto magnitude = detail::finiteMagnitude (sample);
        const auto coefficient = magnitude > value
            ? attackCoefficient : releaseCoefficient;
        value = coefficient * value
              + (static_cast<SampleType> (1) - coefficient) * magnitude;

        if (value < static_cast<SampleType> (1.0e-20))
            value = SampleType {};

        return value;
    }

    SampleType getCurrent() const noexcept { return value; }

private:
    SampleType sampleRate = static_cast<SampleType> (48000);
    SampleType attackCoefficient = SampleType {};
    SampleType releaseCoefficient = SampleType {};
    SampleType value = SampleType {};
};

} // namespace chardsp
