#pragma once

#include <array>
#include <type_traits>

namespace chardsp
{

template <typename SampleType>
struct Downsampler4x
{
    static_assert (std::is_same_v<SampleType, float>
                   || std::is_same_v<SampleType, double>,
                   "Downsampler4x supports float or double");

    void reset() noexcept
    {
        for (auto& filter : filters)
            filter.reset();
    }

    SampleType process (SampleType first, SampleType second,
                        SampleType third, SampleType fourth) noexcept
    {
        filters[0].process (first, second, first);
        filters[0].process (third, fourth, third);
        SampleType output {};
        filters[1].process (first, third, output);
        return output;
    }

    SampleType process (const std::array<SampleType, 4>& samples) noexcept
    {
        return process (samples[0], samples[1], samples[2], samples[3]);
    }

private:
    struct HalfRateFilter
    {
        void reset() noexcept
        {
            a0 = a1 = a2 = a3 = SampleType {};
            b0 = b1 = b2 = b3 = SampleType {};
        }

        void process (SampleType firstInput, SampleType secondInput,
                      SampleType& output) noexcept
        {
            const auto nextA1 = a0 + (secondInput - a1) * static_cast<SampleType> (0.039151598);
            const auto nextA2 = a1 + (nextA1 - a2) * static_cast<SampleType> (0.30264685);
            const auto nextA3 = a2 + (nextA2 - a3) * static_cast<SampleType> (0.6746159);
            const auto nextB1 = b0 + (firstInput - b1) * static_cast<SampleType> (0.14737712);
            const auto nextB2 = b1 + (nextB1 - b2) * static_cast<SampleType> (0.48246855);
            const auto nextB3 = b2 + (nextB2 - b3) * static_cast<SampleType> (0.883005);

            a0 = secondInput;
            a1 = nextA1;
            a2 = nextA2;
            a3 = nextA3;
            b0 = firstInput;
            b1 = nextB1;
            b2 = nextB2;
            b3 = nextB3;
            output = (nextA3 + nextB3) * static_cast<SampleType> (0.5);
        }

        SampleType a0 {};
        SampleType a1 {};
        SampleType a2 {};
        SampleType a3 {};
        SampleType b0 {};
        SampleType b1 {};
        SampleType b2 {};
        SampleType b3 {};
    };

    std::array<HalfRateFilter, 2> filters {};
};

} // namespace chardsp
