#pragma once

#include <cstdint>

namespace chardsp
{

// Finite linear ramp. Once settled, next() is only a branch and return.
struct SmoothedValue
{
    void reset (float value) noexcept
    {
        current = target = value;
        step = 0.0f;
        samplesRemaining = 0;
    }

    void setTarget (float value, uint32_t rampSamples) noexcept
    {
        target = value;

        if (rampSamples == 0)
        {
            reset (value);
            return;
        }

        step = (target - current) / static_cast<float> (rampSamples);
        samplesRemaining = rampSamples;
    }

    float next() noexcept
    {
        if (samplesRemaining != 0)
        {
            current += step;

            if (--samplesRemaining == 0)
                current = target;
        }

        return current;
    }

    float getCurrent() const noexcept { return current; }

private:
    float current = 0.0f;
    float target = 0.0f;
    float step = 0.0f;
    uint32_t samplesRemaining = 0;
};

} // namespace chardsp
