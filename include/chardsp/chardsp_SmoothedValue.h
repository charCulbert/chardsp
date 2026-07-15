#pragma once

#include <cstdint>
#include <type_traits>

namespace chardsp
{

// Finite linear ramp. Once settled, next() is only a branch and return.
template <typename SampleType>
struct SmoothedValue
{
    static_assert (std::is_same_v<SampleType, float>
                   || std::is_same_v<SampleType, double>,
                   "SmoothedValue supports float or double");

    void reset (SampleType value) noexcept
    {
        current = target = value;
        step = SampleType {};
        samplesRemaining = 0;
    }

    void setTarget (SampleType value, uint32_t rampSamples) noexcept
    {
        target = value;

        if (rampSamples == 0)
        {
            reset (value);
            return;
        }

        step = (target - current) / static_cast<SampleType> (rampSamples);
        samplesRemaining = rampSamples;
    }

    SampleType next() noexcept
    {
        if (samplesRemaining != 0)
        {
            current += step;

            if (--samplesRemaining == 0)
                current = target;
        }

        return current;
    }

    SampleType getCurrent() const noexcept { return current; }

private:
    SampleType current = SampleType {};
    SampleType target = SampleType {};
    SampleType step = SampleType {};
    uint32_t samplesRemaining = 0;
};

} // namespace chardsp
