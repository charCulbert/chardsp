#pragma once

#include <algorithm>
#include <cmath>
#include <type_traits>

#include "choc/platform/choc_Assert.h"

namespace chardsp
{

template <typename SampleType>
struct OscillatorPhaseAdvance
{
    SampleType startPhase {};
    SampleType phase {};
    SampleType increment {};
    SampleType wrapOffset {};
    bool wrapped = false;
};

// Reusable normalised phase accumulator. The status form of advance() exposes
// the exact position of a natural wrap inside the sample, allowing a host
// oscillator to create a trigger or schedule a fractional hard-sync reset.
template <typename SampleType>
struct OscillatorPhase
{
    static_assert (std::is_same_v<SampleType, float>
                   || std::is_same_v<SampleType, double>,
                   "OscillatorPhase supports float or double");

    void prepare (SampleType newSampleRate) noexcept
    {
        CHOC_ASSERT (std::isfinite (newSampleRate)
                     && newSampleRate > SampleType {});
        sampleRate = std::isfinite (newSampleRate)
                     && newSampleRate > SampleType {}
            ? newSampleRate : SampleType {};
        frequency = SampleType {};
        phaseIncrement = SampleType {};
        reset();
    }

    void reset (SampleType newPhase = SampleType {}) noexcept
    {
        phase = wrap (newPhase);
    }

    void setFrequency (SampleType newFrequency) noexcept
    {
        if (! std::isfinite (newFrequency))
            newFrequency = newFrequency > SampleType {}
                         && sampleRate > SampleType {}
                ? sampleRate * static_cast<SampleType> (0.49)
                : SampleType {};

        frequency = sampleRate > SampleType {}
            ? std::clamp (newFrequency, SampleType {},
                          sampleRate * static_cast<SampleType> (0.49))
            : SampleType {};
        phaseIncrement = sampleRate > SampleType {}
            ? frequency / sampleRate : SampleType {};
    }

    SampleType getFrequency() const noexcept { return frequency; }
    SampleType getPhase() const noexcept { return phase; }

    SampleType advance() noexcept
    {
        return advanceWithStatus().phase;
    }

    OscillatorPhaseAdvance<SampleType> advanceWithStatus() noexcept
    {
        return advanceBy (phaseIncrement);
    }

    OscillatorPhaseAdvance<SampleType> advanceBy (
        SampleType increment) noexcept
    {
        OscillatorPhaseAdvance<SampleType> result;
        result.startPhase = phase;
        result.increment = std::isfinite (increment)
            ? std::max (increment, SampleType {}) : SampleType {};

        const auto unwrapped = phase + result.increment;
        result.wrapped = unwrapped >= static_cast<SampleType> (1);

        if (result.wrapped)
        {
            result.wrapOffset = result.increment > SampleType {}
                ? (static_cast<SampleType> (1) - phase) / result.increment
                : SampleType {};
            phase = unwrapped - std::floor (unwrapped);
        }
        else
        {
            phase = unwrapped;
        }

        result.phase = phase;
        return result;
    }

    static SampleType wrap (SampleType value) noexcept
    {
        if (! std::isfinite (value))
            return SampleType {};

        auto wrapped = value - std::floor (value);
        if (wrapped < SampleType {})
            wrapped += static_cast<SampleType> (1);
        return wrapped;
    }

    SampleType sampleRate {};
    SampleType frequency {};
    SampleType phase {};
    SampleType phaseIncrement {};
};

} // namespace chardsp
