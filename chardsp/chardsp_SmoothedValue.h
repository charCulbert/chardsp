#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>

namespace chardsp
{

enum class SmoothingMode
{
    linear,
    multiplicative
};

// Finite ramp. Multiplicative mode falls back to a linear ramp when either
// endpoint is zero or the endpoints have different signs.
template <typename SampleType>
struct SmoothedValue
{
    static_assert (std::is_same_v<SampleType, float>
                   || std::is_same_v<SampleType, double>,
                   "SmoothedValue supports float or double");

    void reset (SampleType value) noexcept
    {
        current = target = sanitise (value);
        step = SampleType {};
        multiplier = static_cast<SampleType> (1);
        samplesRemaining = 0;
    }

    void setTarget (SampleType value, uint32_t rampSamples,
                    SmoothingMode nextMode = SmoothingMode::linear) noexcept
    {
        target = sanitise (value);

        if (rampSamples == 0)
        {
            reset (target);
            return;
        }

        mode = nextMode;

        if (mode == SmoothingMode::multiplicative
            && current != SampleType {} && target != SampleType {}
            && std::signbit (current) == std::signbit (target))
        {
            step = SampleType {};
            multiplier = std::pow (target / current,
                static_cast<SampleType> (1)
                    / static_cast<SampleType> (rampSamples));
        }
        else
        {
            mode = SmoothingMode::linear;
            step = (target - current)
                 / static_cast<SampleType> (rampSamples);
            multiplier = static_cast<SampleType> (1);
        }

        samplesRemaining = rampSamples;
    }

    SampleType next() noexcept
    {
        if (samplesRemaining != 0)
        {
            current = mode == SmoothingMode::multiplicative
                ? current * multiplier : current + step;

            if (--samplesRemaining == 0)
                current = target;
        }

        return current;
    }

    SampleType getCurrent() const noexcept { return current; }
    SampleType getTarget() const noexcept { return target; }
    bool isSmoothing() const noexcept { return samplesRemaining != 0; }

    SampleType skip (uint32_t samples) noexcept
    {
        if (samples >= samplesRemaining)
        {
            if (samplesRemaining != 0)
                current = target;

            samplesRemaining = 0;
            return current;
        }

        if (samples != 0)
        {
            current = mode == SmoothingMode::multiplicative
                ? current * std::pow (multiplier,
                    static_cast<SampleType> (samples))
                : current + step * static_cast<SampleType> (samples);
            samplesRemaining -= samples;
        }

        return current;
    }

private:
    static SampleType sanitise (SampleType value) noexcept
    {
        return std::isfinite (value) ? value : SampleType {};
    }

    SampleType current = SampleType {};
    SampleType target = SampleType {};
    SampleType step = SampleType {};
    SampleType multiplier = static_cast<SampleType> (1);
    uint32_t samplesRemaining = 0;
    SmoothingMode mode = SmoothingMode::linear;
};

} // namespace chardsp
