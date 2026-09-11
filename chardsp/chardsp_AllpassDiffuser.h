#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <type_traits>

#include "chardsp/chardsp_FractionalDelayLine.h"

namespace chardsp
{

// Series of Schroeder allpass stages in the two-multiply form, the classic
// diffuser. Stage delays are millisecond values converted at the prepared rate
// and never fall below one sample; a gain of zero leaves a plain delay chain.
// Before prepare, process returns its input untouched.
template <typename SampleType, size_t stages = 4>
struct AllpassDiffuser
{
    static_assert (std::is_same_v<SampleType, float>
                   || std::is_same_v<SampleType, double>,
                   "AllpassDiffuser supports float or double");
    static_assert (stages > 0, "AllpassDiffuser needs at least one stage");

    void prepare (SampleType newSampleRate,
                  SampleType maximumStageDelayInMilliseconds)
    {
        sampleRate = std::isfinite (newSampleRate)
                && newSampleRate > SampleType {}
            ? newSampleRate : static_cast<SampleType> (48000);

        const auto maximumSamples = static_cast<size_t> (
            millisecondsToSamples (maximumStageDelayInMilliseconds));

        for (auto& line : lines)
            line.prepare (maximumSamples);

        prepared = true;
        reset();
    }

    void reset() noexcept
    {
        for (auto& line : lines)
            line.reset();
    }

    void setStageDelay (size_t stage,
                        SampleType delayInMilliseconds) noexcept
    {
        if (stage >= stages)
            return;

        const auto samples = millisecondsToSamples (delayInMilliseconds);
        lines[stage].setDelay (samples > static_cast<SampleType> (1)
            ? samples - static_cast<SampleType> (1) : SampleType {});
    }

    void setGain (SampleType newGain) noexcept
    {
        gain = std::isfinite (newGain)
            ? std::clamp (newGain, static_cast<SampleType> (-0.98),
                          static_cast<SampleType> (0.98))
            : SampleType {};
    }

    SampleType getGain() const noexcept { return gain; }

    SampleType process (SampleType sample) noexcept
    {
        sample = std::isfinite (sample) ? sample : SampleType {};

        if (! prepared)
            return sample;

        if (gain == SampleType {})
        {
            for (auto& line : lines)
            {
                const auto delayed = line.read();
                line.write (sample);
                sample = delayed;
            }

            return sample;
        }

        for (auto& line : lines)
        {
            const auto delayed = line.read();
            auto stored = sample + gain * delayed;
            stored = std::abs (stored)
                    < static_cast<SampleType> (1.0e-20)
                ? SampleType {} : stored;
            line.write (stored);
            sample = delayed - gain * stored;
        }

        return sample;
    }

private:
    SampleType millisecondsToSamples (SampleType milliseconds) const noexcept
    {
        const auto clamped = std::isfinite (milliseconds)
            ? std::clamp (milliseconds, SampleType {},
                          static_cast<SampleType> (10000))
            : SampleType {};
        return clamped * sampleRate / static_cast<SampleType> (1000);
    }

    std::array<FractionalDelayLine<SampleType>, stages> lines;
    SampleType sampleRate = static_cast<SampleType> (48000);
    SampleType gain = static_cast<SampleType> (0.5);
    bool prepared = false;
};

} // namespace chardsp
