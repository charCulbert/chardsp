#pragma once

#include <algorithm>
#include <cmath>

namespace chardsp
{

enum class FilterMode
{
    lowpass,
    highpass
};

// Two-integrator-loop topology-preserving-transform state-variable filter.
struct StateVariableFilter
{
    void prepare (float newSampleRate) noexcept
    {
        sampleRate = std::isfinite (newSampleRate) && newSampleRate > 0.0f
            ? newSampleRate
            : 48000.0f;
        mode = FilterMode::lowpass;
        setCutoffAndQ (20000.0f, 0.5f);
        reset();
    }

    void reset() noexcept
    {
        z0 = 0.0f;
        z1 = 0.0f;
    }

    void setMode (FilterMode nextMode) noexcept
    {
        mode = nextMode;
    }

    void setCutoffAndQ (float cutoffHz, float q) noexcept
    {
        const auto processorFrequency = static_cast<double> (sampleRate);
        const auto requestedCutoff = std::isnan (cutoffHz)
            ? 0.0
            : static_cast<double> (cutoffHz);
        const auto filterFrequency = std::clamp (requestedCutoff,
                                                 0.0,
                                                 processorFrequency * 0.48);
        const auto f = std::tan (3.14159265358979323846 * filterFrequency
                                 / processorFrequency);
        const auto requestedQ = std::isnan (q) ? 0.0 : static_cast<double> (q);
        const auto inverseQ = 1.0 / std::max (0.000001, requestedQ);
        d = static_cast<float> (1.0 / ((1.0 + inverseQ * f) + f * f));
        a = static_cast<float> (f);
        g1 = static_cast<float> (f + inverseQ);
    }

    float process (float input) noexcept
    {
        const auto high = ((input - g1 * z0) - z1) * d;
        const auto band = a * high + z0;
        const auto low = a * band + z1;
        z0 = a * high + band;
        z1 = a * band + low;
        return mode == FilterMode::highpass ? high : low;
    }

private:
    float sampleRate = 48000.0f;
    float a = 0.0f;
    float d = 1.0f;
    float g1 = 0.0f;
    float z0 = 0.0f;
    float z1 = 0.0f;
    FilterMode mode = FilterMode::lowpass;
};

} // namespace chardsp
