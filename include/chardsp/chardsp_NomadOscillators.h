#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <type_traits>

#include "chardsp_Downsampler4x.h"

namespace chardsp
{

enum class NomadOscillatorWaveform
{
    saw,
    ramp,
    pulse,
    triangle,
    sine
};

// Nomad's shaped oscillator family. Unlike the generic oscillators in
// chardsp_Oscillator.h, this deliberately owns Nomad's waveform-shape policy
// and its 4x rendering/downsampling path.
template <typename SampleType>
struct NomadOscillator
{
    static_assert (std::is_same_v<SampleType, float>
                   || std::is_same_v<SampleType, double>,
                   "NomadOscillator supports float or double");

    void prepare (SampleType newSampleRate) noexcept
    {
        sampleRate = std::isfinite (newSampleRate)
                  && newSampleRate > SampleType {}
            ? newSampleRate : SampleType {};
        frequency = SampleType {};
        phaseIncrement = SampleType {};
        waveform = NomadOscillatorWaveform::saw;
        shape = SampleType {};
        reset();
    }

    void reset (SampleType newPhase = SampleType {}) noexcept
    {
        phase = std::isfinite (newPhase)
            ? newPhase - std::floor (newPhase) : SampleType {};

        if (phase < SampleType {})
            phase += static_cast<SampleType> (1);

        cycleIndex = 0;
        downsampler.reset();
        downsamplerNeedsReset = false;
    }

    void setWaveform (NomadOscillatorWaveform nextWaveform) noexcept
    {
        if (waveform != nextWaveform)
        {
            downsampler.reset();
            downsamplerNeedsReset = false;
        }

        waveform = nextWaveform;
    }

    void setFrequency (SampleType newFrequency) noexcept
    {
        if (! std::isfinite (newFrequency))
            newFrequency = newFrequency > SampleType {}
                         && sampleRate > SampleType {}
                ? sampleRate * static_cast<SampleType> (0.49)
                : SampleType {};

        frequency = sampleRate > SampleType {}
            ? std::clamp (newFrequency, SampleType {},
                          sampleRate * static_cast<SampleType> (0.49))
            : SampleType {};
        phaseIncrement = sampleRate > SampleType {}
            ? frequency / (sampleRate * static_cast<SampleType> (4))
            : SampleType {};
    }

    void setShape (SampleType nextShape) noexcept
    {
        shape = std::isfinite (nextShape)
            ? std::clamp (nextShape, SampleType {},
                          static_cast<SampleType> (1))
            : SampleType {};
    }

    NomadOscillatorWaveform getWaveform() const noexcept { return waveform; }
    SampleType getFrequency() const noexcept { return frequency; }
    SampleType getShape() const noexcept { return shape; }

    void advance() noexcept
    {
        downsamplerNeedsReset = true;

        if (phaseIncrement <= SampleType {})
            return;

        for (int oversample = 0; oversample < 4; ++oversample)
            advancePhase();
    }

    SampleType next() noexcept
    {
        if (phaseIncrement <= SampleType {})
            return SampleType {};

        if (downsamplerNeedsReset)
        {
            downsampler.reset();
            downsamplerNeedsReset = false;
        }

        std::array<SampleType, 4> oversampled {};

        for (auto& sample : oversampled)
        {
            sample = renderAt (phase);
            advancePhase();
        }

        return downsampler.process (oversampled);
    }

private:
    static constexpr SampleType twoPi = static_cast<SampleType> (
        6.28318530717958647692);

    void advancePhase() noexcept
    {
        phase += phaseIncrement;

        if (phase >= static_cast<SampleType> (1))
        {
            phase -= static_cast<SampleType> (1);
            ++cycleIndex;
        }
    }

    SampleType renderAt (SampleType currentPhase) const noexcept
    {
        switch (waveform)
        {
            case NomadOscillatorWaveform::saw:
                return renderSaw (currentPhase,
                                  shape * static_cast<SampleType> (2));
            case NomadOscillatorWaveform::ramp:
                return renderSaw (currentPhase,
                                  static_cast<SampleType> (2)
                                      + shape * static_cast<SampleType> (2));
            case NomadOscillatorWaveform::pulse:
                return currentPhase < pulseWidth()
                    ? static_cast<SampleType> (1)
                    : static_cast<SampleType> (-1);
            case NomadOscillatorWaveform::triangle:
                return mirrorFold (rawTriangle (currentPhase) * foldDrive());
            case NomadOscillatorWaveform::sine:
                return mirrorFold (std::sin (currentPhase * twoPi)
                                   * foldDrive());
        }

        return SampleType {};
    }

    SampleType renderSaw (SampleType currentPhase,
                          SampleType sawShape) const noexcept
    {
        const auto one = static_cast<SampleType> (1);
        const auto two = static_cast<SampleType> (2);
        const auto three = static_cast<SampleType> (3);
        const auto four = static_cast<SampleType> (4);
        const auto foldDepth = sawShape < one ? sawShape
                             : sawShape < two ? two - sawShape
                             : sawShape < three ? sawShape - two
                                                : four - sawShape;
        const auto tau = one - foldDepth;
        const auto baseSign = sawShape < one || sawShape >= three ? one : -one;
        const auto saw = baseSign * (one - two * currentPhase);

        if (tau >= static_cast<SampleType> (0.999999))
            return saw;

        const auto cyclePolarity = (cycleIndex & 1) == 0 ? -one : one;
        const auto polarity = sawShape < two ? cyclePolarity : -cyclePolarity;
        const auto folded = polarity * saw < -tau ? -saw : saw;
        return (two * folded - polarity * (one - tau)) / (one + tau);
    }

    SampleType pulseWidth() const noexcept
    {
        constexpr auto edgeGuard = static_cast<SampleType> (0.02);
        return edgeGuard + shape * (static_cast<SampleType> (1)
                                    - static_cast<SampleType> (2) * edgeGuard);
    }

    SampleType foldDrive() const noexcept
    {
        return static_cast<SampleType> (1)
             + shape * static_cast<SampleType> (3);
    }

    static SampleType rawTriangle (SampleType currentPhase) noexcept
    {
        return currentPhase < static_cast<SampleType> (0.5)
            ? currentPhase * static_cast<SampleType> (4)
                - static_cast<SampleType> (1)
            : static_cast<SampleType> (3)
                - currentPhase * static_cast<SampleType> (4);
    }

    static SampleType mirrorFold (SampleType value) noexcept
    {
        auto folded = value;

        if (folded > static_cast<SampleType> (1))
        {
            folded = static_cast<SampleType> (2) - folded;

            if (folded < static_cast<SampleType> (-1))
                folded = static_cast<SampleType> (-2) - folded;
        }
        else if (folded < static_cast<SampleType> (-1))
        {
            folded = static_cast<SampleType> (-2) - folded;

            if (folded > static_cast<SampleType> (1))
                folded = static_cast<SampleType> (2) - folded;
        }

        return folded;
    }

    Downsampler4x<SampleType> downsampler;
    NomadOscillatorWaveform waveform = NomadOscillatorWaveform::saw;
    SampleType sampleRate {};
    SampleType frequency {};
    SampleType phase {};
    SampleType phaseIncrement {};
    SampleType shape {};
    int cycleIndex = 0;
    bool downsamplerNeedsReset = false;
};

} // namespace chardsp
