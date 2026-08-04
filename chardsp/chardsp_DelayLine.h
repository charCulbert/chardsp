#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace chardsp
{

// Fixed-capacity delay storage. The write position always names the next slot;
// a delay of zero reads the most recently written sample and maximumDelay reads
// the oldest sample retained by the line.
template <typename SampleType, size_t capacity>
struct DelayLine
{
    static_assert (std::is_same_v<SampleType, float>
                   || std::is_same_v<SampleType, double>,
                   "DelayLine supports float or double");
    static_assert (capacity > 0, "DelayLine capacity must be nonzero");

    static constexpr size_t maximumDelay = capacity - 1;

    void reset() noexcept
    {
        samples.fill (SampleType {});
        writePosition = 0;
    }

    void setDelay (SampleType delayInSamples) noexcept
    {
        delay = sanitiseDelay (delayInSamples);
    }

    SampleType getDelay() const noexcept { return delay; }

    void write (SampleType sample) noexcept
    {
        samples[writePosition] = std::isfinite (sample)
            ? sample : SampleType {};

        if (++writePosition == capacity)
            writePosition = 0;
    }

    SampleType read() const noexcept { return readLinear (delay); }

    SampleType read (size_t delayInSamples) const noexcept
    {
        return sampleAtDelay (std::min (delayInSamples, maximumDelay));
    }

    SampleType readLinear (SampleType delayInSamples) const noexcept
    {
        const auto clamped = sanitiseDelay (delayInSamples);
        const auto integral = static_cast<size_t> (clamped);
        const auto fraction = clamped - static_cast<SampleType> (integral);
        const auto first = sampleAtDelay (integral);

        if (fraction == SampleType {} || integral == maximumDelay)
            return first;

        const auto second = sampleAtDelay (integral + 1);
        return first * (static_cast<SampleType> (1) - fraction)
             + second * fraction;
    }

    SampleType process (SampleType sample) noexcept
    {
        write (sample);
        return read();
    }

private:
    static SampleType sanitiseDelay (SampleType value) noexcept
    {
        return std::isfinite (value)
            ? std::clamp (value, SampleType {},
                          static_cast<SampleType> (maximumDelay))
            : SampleType {};
    }

    SampleType sampleAtDelay (size_t delayInSamples) const noexcept
    {
        const auto newest = writePosition == 0 ? capacity - 1
                                                : writePosition - 1;
        const auto offset = delayInSamples % capacity;
        const auto index = newest >= offset ? newest - offset
                                             : capacity + newest - offset;
        return samples[index];
    }

    std::array<SampleType, capacity> samples {};
    size_t writePosition = 0;
    SampleType delay = SampleType {};
};

} // namespace chardsp
