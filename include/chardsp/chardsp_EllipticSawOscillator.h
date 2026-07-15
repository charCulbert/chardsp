#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <type_traits>

#include "choc/platform/choc_Assert.h"
#include "choc/platform/choc_DisableAllWarnings.h"
#include "elliptic-blep.h"
#include "choc/platform/choc_ReenableAllWarnings.h"

namespace chardsp
{

template <typename SampleType>
struct EllipticSawOscillator
{
    static_assert (std::is_same_v<SampleType, float>
                   || std::is_same_v<SampleType, double>,
                   "EllipticSawOscillator supports float or double");

    void prepare (SampleType newSampleRate) noexcept
    {
        CHOC_ASSERT (std::isfinite (newSampleRate) && newSampleRate > 0.0f);

        if (! std::isfinite (newSampleRate) || newSampleRate <= 0.0f)
        {
            sampleRate = SampleType {};
            phaseIncrement = SampleType {};
            blep.reset();
            return;
        }

        sampleRate = newSampleRate;
        phase = SampleType {};
        blep.emplace (sampleRate);
        allpass.reset();
        updatePhaseIncrement();
    }

    void setFrequency (SampleType newFrequency) noexcept
    {
        if (! std::isfinite (newFrequency))
            newFrequency = newFrequency > 0.0f && sampleRate > 0.0f
                ? sampleRate * static_cast<SampleType> (0.49)
                : SampleType {};

        frequency = std::max (newFrequency, SampleType {});
        updatePhaseIncrement();
    }

    SampleType next() noexcept
    {
        if (! blep || phaseIncrement == 0.0f)
            return SampleType {};

        phase += phaseIncrement;
        blep->step();

        if (phase >= static_cast<SampleType> (1.0))
        {
            const auto samplesInPast = (phase - static_cast<SampleType> (1.0))
                                     / phaseIncrement;
            phase -= static_cast<SampleType> (1.0);
            blep->add (static_cast<SampleType> (-2.0), 1, samplesInPast);
        }

        return allpass (static_cast<SampleType> (2.0) * phase
                        - static_cast<SampleType> (1.0) + blep->get());
    }

private:
    void updatePhaseIncrement() noexcept
    {
        if (sampleRate <= SampleType {})
        {
            phaseIncrement = SampleType {};
            return;
        }

        frequency = std::clamp (frequency, SampleType {},
                                sampleRate * static_cast<SampleType> (0.49));
        phaseIncrement = frequency / sampleRate;
    }

    std::optional<signalsmith::blep::EllipticBlep<SampleType>> blep;
    signalsmith::blep::EllipticBlepAllpass<SampleType> allpass;
    SampleType sampleRate {};
    SampleType frequency {};
    SampleType phase {};
    SampleType phaseIncrement {};
};

} // namespace chardsp
