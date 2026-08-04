#pragma once

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace chardsp
{

// First-order DC blocker. The cutoff is the 1/e pole frequency. A cutoff of
// zero explicitly bypasses the filter.
template <typename SampleType>
struct DCBlocker
{
    static_assert (std::is_same_v<SampleType, float>
                   || std::is_same_v<SampleType, double>,
                   "DCBlocker supports float or double");

    void prepare (SampleType newSampleRate) noexcept
    {
        sampleRate = std::isfinite (newSampleRate)
                && newSampleRate > SampleType {}
            ? newSampleRate : static_cast<SampleType> (48000);
        setCutoff (cutoff);
        reset();
    }

    void reset() noexcept
    {
        previousInput = SampleType {};
        previousOutput = SampleType {};
    }

    void setCutoff (SampleType cutoffHz) noexcept
    {
        cutoff = std::isfinite (cutoffHz)
            ? std::clamp (cutoffHz, SampleType {},
                          sampleRate * static_cast<SampleType> (0.49))
            : static_cast<SampleType> (20);
        coefficient = cutoff == SampleType {}
            ? SampleType {}
            : std::exp (static_cast<SampleType> (-6.28318530717958647692)
                        * cutoff / sampleRate);
    }

    SampleType getCutoff() const noexcept { return cutoff; }

    SampleType process (SampleType input) noexcept
    {
        input = std::isfinite (input) ? input : SampleType {};

        if (cutoff == SampleType {})
        {
            previousInput = input;
            previousOutput = SampleType {};
            return input;
        }

        auto output = input - previousInput
                    + coefficient * previousOutput;
        previousInput = input;
        previousOutput = std::abs (output)
                < static_cast<SampleType> (1.0e-20)
            ? SampleType {} : output;
        return previousOutput;
    }

private:
    SampleType sampleRate = static_cast<SampleType> (48000);
    SampleType cutoff = static_cast<SampleType> (20);
    SampleType coefficient = SampleType {};
    SampleType previousInput = SampleType {};
    SampleType previousOutput = SampleType {};
};

} // namespace chardsp
