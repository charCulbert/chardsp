#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <type_traits>
#include <vector>

namespace chardsp
{

// Runtime-capacity delay storage with 4-point Catmull-Rom interpolation and
// separate reads and writes for feedback. Taps are named in samples ago, so a
// delay of zero is the most recently written sample; below a delay of one the
// newer kernel tap does not exist yet and is clamped to that newest sample,
// which degrades the interpolation to linear there. Before prepare every read
// returns zero and every write is discarded.
template <typename SampleType>
struct FractionalDelayLine
{
    static_assert (std::is_same_v<SampleType, float>
                   || std::is_same_v<SampleType, double>,
                   "FractionalDelayLine supports float or double");

    void prepare (size_t maximumDelayInSamples)
    {
        maximumDelay = maximumDelayInSamples;
        samples.assign (maximumDelay + 4, SampleType {});
        reset();
    }

    void reset() noexcept
    {
        std::fill (samples.begin(), samples.end(), SampleType {});
        writePosition = 0;
    }

    size_t getMaximumDelay() const noexcept { return maximumDelay; }

    void setDelay (SampleType delayInSamples) noexcept
    {
        delay = sanitiseDelay (delayInSamples);
    }

    SampleType getDelay() const noexcept { return delay; }

    void write (SampleType sample) noexcept
    {
        if (samples.empty())
            return;

        samples[writePosition] = std::isfinite (sample)
            ? sample : SampleType {};

        if (++writePosition == samples.size())
            writePosition = 0;
    }

    SampleType read() const noexcept { return readCubic (delay); }

    SampleType read (size_t delayInSamples) const noexcept
    {
        return sampleAtDelay (std::min (delayInSamples, maximumDelay));
    }

    SampleType readCubic (SampleType delayInSamples) const noexcept
    {
        const auto clamped = sanitiseDelay (delayInSamples);
        const auto integral = static_cast<size_t> (clamped);
        const auto fraction = clamped - static_cast<SampleType> (integral);
        const auto y1 = sampleAtDelay (integral);

        if (fraction == SampleType {})
            return y1;

        const auto y0 = sampleAtDelay (integral == 0 ? 0 : integral - 1);
        const auto y2 = sampleAtDelay (integral + 1);
        const auto y3 = sampleAtDelay (integral + 2);
        const auto a0 = static_cast<SampleType> (-0.5) * y0
                      + static_cast<SampleType> (1.5) * y1
                      - static_cast<SampleType> (1.5) * y2
                      + static_cast<SampleType> (0.5) * y3;
        const auto a1 = y0 - static_cast<SampleType> (2.5) * y1
                      + static_cast<SampleType> (2) * y2
                      - static_cast<SampleType> (0.5) * y3;
        const auto a2 = static_cast<SampleType> (0.5) * (y2 - y0);
        return ((a0 * fraction + a1) * fraction + a2) * fraction + y1;
    }

    SampleType process (SampleType sample) noexcept
    {
        write (sample);
        return read();
    }

private:
    SampleType sanitiseDelay (SampleType value) const noexcept
    {
        return std::isfinite (value)
            ? std::clamp (value, SampleType {},
                          static_cast<SampleType> (maximumDelay))
            : SampleType {};
    }

    SampleType sampleAtDelay (size_t delayInSamples) const noexcept
    {
        if (samples.empty())
            return SampleType {};

        const auto capacity = samples.size();
        const auto newest = writePosition == 0 ? capacity - 1
                                               : writePosition - 1;
        const auto offset = delayInSamples % capacity;
        const auto index = newest >= offset ? newest - offset
                                            : capacity + newest - offset;
        return samples[index];
    }

    std::vector<SampleType> samples;
    size_t maximumDelay = 0;
    size_t writePosition = 0;
    SampleType delay = SampleType {};
};

} // namespace chardsp
