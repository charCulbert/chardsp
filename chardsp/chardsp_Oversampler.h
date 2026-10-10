#pragma once

#include <cassert>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

#include "signalsmith-dsp/rates.h"

namespace chardsp
{

enum class OversamplingFactor : uint8_t
{
    x2 = 2,
    x4 = 4
};

namespace detail
{

template <typename SampleType>
using SignalsmithOversampler2x =
    signalsmith::rates::Oversampler2xFIR<SampleType>;

} // namespace detail

// Block oversampling with prepared scratch storage. Signalsmith implements each
// internal 2x stage; the public API takes and gives plain channel pointers.
template <typename SampleType>
class Oversampler
{
public:
    static_assert (std::is_same_v<SampleType, float>
                   || std::is_same_v<SampleType, double>,
                   "Oversampler supports float or double");

    bool prepare (uint32_t channels, uint32_t maximumBlockSize,
                  OversamplingFactor newFactor =
                      OversamplingFactor::x2)
    {
        if ((newFactor != OversamplingFactor::x2
             && newFactor != OversamplingFactor::x4)
            || channels == 0 || maximumBlockSize == 0
            || channels > static_cast<uint32_t> (
                std::numeric_limits<int>::max())
            || maximumBlockSize > static_cast<uint32_t> (
                std::numeric_limits<int>::max() / 2))
            return false;

        numChannels = channels;
        maxBlockSize = maximumBlockSize;
        factor = newFactor;
        stage1.resize (static_cast<int> (channels),
                       static_cast<int> (maximumBlockSize));

        if (factor == OversamplingFactor::x4)
            stage2.resize (static_cast<int> (channels),
                           static_cast<int> (maximumBlockSize * 2));

        channelPointers.resize (channels);
        prepared = true;
        reset();
        return true;
    }

    void reset() noexcept
    {
        if (prepared)
        {
            stage1.reset();

            if (factor == OversamplingFactor::x4)
                stage2.reset();
        }

        currentBlockSize = 0;
        hasUpsampledBlock = false;
    }

    bool canProcess (uint32_t channels,
                     uint32_t blockSize) const noexcept
    {
        return prepared && channels == numChannels
            && blockSize <= maxBlockSize;
    }

    // The high-rate block for numSamples of each input channel: numChannels
    // channels of numSamples · factor samples, to process in place before
    // downsample(). Null, and an assertion, unless canProcess().
    SampleType* const* upsample (const SampleType* const* input,
                                 uint32_t channels,
                                 uint32_t numSamples) noexcept
    {
        const auto valid = canProcess (channels, numSamples);
        assert (valid);

        if (! valid)
            return nullptr;

        currentBlockSize = numSamples;
        const auto lowSamples = static_cast<int> (currentBlockSize);

        for (uint32_t channel = 0; channel < numChannels; ++channel)
            stage1.upChannel (static_cast<int> (channel), input[channel],
                              lowSamples);

        if (factor == OversamplingFactor::x4)
        {
            for (uint32_t channel = 0; channel < numChannels; ++channel)
            {
                stage2.upChannel (
                    static_cast<int> (channel), stage1[channel],
                    lowSamples * 2);
                channelPointers[channel] = stage2[channel];
            }
        }
        else
        {
            for (uint32_t channel = 0; channel < numChannels; ++channel)
                channelPointers[channel] = stage1[channel];
        }

        hasUpsampledBlock = true;
        return channelPointers.data();
    }

    // The high-rate block back down into numSamples of each output channel,
    // the same counts upsample() took. False, and an assertion, otherwise.
    bool downsample (SampleType* const* output, uint32_t channels,
                     uint32_t numSamples) noexcept
    {
        const auto valid = hasUpsampledBlock && channels == numChannels
            && numSamples == currentBlockSize;
        assert (valid);

        if (! valid)
            return false;

        const auto lowSamples = static_cast<int> (currentBlockSize);

        if (factor == OversamplingFactor::x4)
            for (uint32_t channel = 0; channel < numChannels; ++channel)
                stage2.downChannel (static_cast<int> (channel),
                                    stage1[channel], lowSamples * 2);

        for (uint32_t channel = 0; channel < numChannels; ++channel)
            stage1.downChannel (static_cast<int> (channel), output[channel],
                                lowSamples);

        hasUpsampledBlock = false;
        return true;
    }

    uint32_t getLatency() const noexcept
    {
        if (! prepared)
            return 0;

        auto latency = static_cast<uint32_t> (stage1.latency());

        if (factor == OversamplingFactor::x4)
            latency += static_cast<uint32_t> (stage2.latency() / 2);

        return latency;
    }

    OversamplingFactor getFactor() const noexcept { return factor; }
    uint32_t getNumChannels() const noexcept { return numChannels; }
    uint32_t getMaximumBlockSize() const noexcept { return maxBlockSize; }

private:
    detail::SignalsmithOversampler2x<SampleType> stage1;
    detail::SignalsmithOversampler2x<SampleType> stage2;
    std::vector<SampleType*> channelPointers;
    uint32_t numChannels = 0;
    uint32_t maxBlockSize = 0;
    uint32_t currentBlockSize = 0;
    OversamplingFactor factor = OversamplingFactor::x2;
    bool prepared = false;
    bool hasUpsampledBlock = false;
};

} // namespace chardsp
