#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

#include "choc/platform/choc_Assert.h"
#include "choc/platform/choc_DisableAllWarnings.h"
#include "elliptic-blep.h"
#include "choc/platform/choc_ReenableAllWarnings.h"

namespace chardsp
{

struct EllipticSawOscillator
{
    void prepare (float newSampleRate) noexcept
    {
        CHOC_ASSERT (std::isfinite (newSampleRate) && newSampleRate > 0.0f);

        if (! std::isfinite (newSampleRate) || newSampleRate <= 0.0f)
        {
            sampleRate = 0.0f;
            phaseIncrement = 0.0f;
            blep.reset();
            return;
        }

        sampleRate = newSampleRate;
        phase = 0.0f;
        blep.emplace (sampleRate);
        allpass.reset();
        updatePhaseIncrement();
    }

    void setFrequency (float newFrequency) noexcept
    {
        if (! std::isfinite (newFrequency))
            newFrequency = newFrequency > 0.0f && sampleRate > 0.0f
                ? sampleRate * 0.49f
                : 0.0f;

        frequency = std::max (newFrequency, 0.0f);
        updatePhaseIncrement();
    }

    float next() noexcept
    {
        if (! blep || phaseIncrement == 0.0f)
            return 0.0f;

        phase += phaseIncrement;
        blep->step();

        if (phase >= 1.0f)
        {
            const auto samplesInPast = (phase - 1.0f) / phaseIncrement;
            phase -= 1.0f;
            blep->add (-2.0f, 1, samplesInPast);
        }

        return allpass (2.0f * phase - 1.0f + blep->get());
    }

private:
    void updatePhaseIncrement() noexcept
    {
        if (sampleRate <= 0.0f)
        {
            phaseIncrement = 0.0f;
            return;
        }

        frequency = std::clamp (frequency, 0.0f, sampleRate * 0.49f);
        phaseIncrement = frequency / sampleRate;
    }

    std::optional<signalsmith::blep::EllipticBlep<float>> blep;
    signalsmith::blep::EllipticBlepAllpass<float> allpass;
    float sampleRate = 0.0f;
    float frequency = 0.0f;
    float phase = 0.0f;
    float phaseIncrement = 0.0f;
};

} // namespace chardsp
