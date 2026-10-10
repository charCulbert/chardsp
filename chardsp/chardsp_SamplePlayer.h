#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <type_traits>

namespace chardsp
{

// Minimal forward player over caller-owned mono sample memory. Positions are in
// source samples and linear interpolation is used between adjacent values.
template <typename SampleType>
struct SamplePlayer
{
    static_assert (std::is_same_v<SampleType, float>
                   || std::is_same_v<SampleType, double>,
                   "SamplePlayer supports float or double");

    // numSamples samples the caller owns, read where they are.
    void setSampleData (const SampleType* samples, size_t numSamples) noexcept
    {
        data = samples;
        size = samples != nullptr ? numSamples : 0;
        reset();
    }

    void reset() noexcept
    {
        position = SampleType {};
        playing = false;
    }

    void start (SampleType startPosition = SampleType {}) noexcept
    {
        position = sanitisePosition (startPosition);
        playing = size > 0 && position < static_cast<SampleType> (size);
    }

    void stop() noexcept { playing = false; }

    void setRate (SampleType sourceSamplesPerOutputSample) noexcept
    {
        rate = std::isfinite (sourceSamplesPerOutputSample)
            ? std::max (SampleType {}, sourceSamplesPerOutputSample)
            : static_cast<SampleType> (1);
    }

    void setPosition (SampleType nextPosition) noexcept
    {
        position = sanitisePosition (nextPosition);

        if (position >= static_cast<SampleType> (size))
            playing = false;
    }

    SampleType process() noexcept
    {
        if (! playing || size == 0
            || position >= static_cast<SampleType> (size))
        {
            playing = false;
            return SampleType {};
        }

        const auto firstIndex = static_cast<size_t> (position);
        const auto secondIndex = std::min (firstIndex + 1, size - 1);
        const auto fraction = position
            - static_cast<SampleType> (firstIndex);
        const auto output = data[firstIndex]
            + (data[secondIndex] - data[firstIndex]) * fraction;
        position += rate;

        if (position >= static_cast<SampleType> (size))
            playing = false;

        return output;
    }

    bool isPlaying() const noexcept { return playing; }
    SampleType getPosition() const noexcept { return position; }
    SampleType getRate() const noexcept { return rate; }

private:
    static SampleType sanitisePosition (SampleType value) noexcept
    {
        return std::isfinite (value) ? std::max (SampleType {}, value)
                                     : SampleType {};
    }

    const SampleType* data = nullptr;
    size_t size = 0;
    SampleType position = SampleType {};
    SampleType rate = static_cast<SampleType> (1);
    bool playing = false;
};

} // namespace chardsp
