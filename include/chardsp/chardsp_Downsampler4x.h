#pragma once

#include <array>

namespace chardsp
{

struct Downsampler4x
{
    void reset() noexcept
    {
        for (auto& filter : filters)
            filter.reset();
    }

    float process (float first, float second, float third, float fourth) noexcept
    {
        filters[0].process (first, second, first);
        filters[0].process (third, fourth, third);
        float output = 0.0f;
        filters[1].process (first, third, output);
        return output;
    }

    float process (const std::array<float, 4>& samples) noexcept
    {
        return process (samples[0], samples[1], samples[2], samples[3]);
    }

private:
    struct HalfRateFilter
    {
        void reset() noexcept
        {
            a0 = a1 = a2 = a3 = 0.0f;
            b0 = b1 = b2 = b3 = 0.0f;
        }

        void process (float firstInput, float secondInput, float& output) noexcept
        {
            const auto nextA1 = a0 + (secondInput - a1) * 0.039151598f;
            const auto nextA2 = a1 + (nextA1 - a2) * 0.30264685f;
            const auto nextA3 = a2 + (nextA2 - a3) * 0.6746159f;
            const auto nextB1 = b0 + (firstInput - b1) * 0.14737712f;
            const auto nextB2 = b1 + (nextB1 - b2) * 0.48246855f;
            const auto nextB3 = b2 + (nextB2 - b3) * 0.883005f;

            a0 = secondInput;
            a1 = nextA1;
            a2 = nextA2;
            a3 = nextA3;
            b0 = firstInput;
            b1 = nextB1;
            b2 = nextB2;
            b3 = nextB3;
            output = (nextA3 + nextB3) * 0.5f;
        }

        float a0 = 0.0f;
        float a1 = 0.0f;
        float a2 = 0.0f;
        float a3 = 0.0f;
        float b0 = 0.0f;
        float b1 = 0.0f;
        float b2 = 0.0f;
        float b3 = 0.0f;
    };

    std::array<HalfRateFilter, 2> filters {};
};

} // namespace chardsp
