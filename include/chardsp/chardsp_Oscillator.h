#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <type_traits>

#include "chardsp/chardsp_Downsampler4x.h"

namespace chardsp
{

enum class OscillatorWaveform
{
    saw,
    ramp,
    pulse,
    triangle,
    sine
};

// The five pitched waveform/shape equations from Nomad's oscillator, rendered
// at 4x and passed through Nomad's fixed two-stage downsampler. Noise remains a
// separate ColoredNoise source so a synth can mix it independently.
template <typename SampleType>
struct Oscillator
{
    static_assert (std::is_same_v<SampleType, float>
                   || std::is_same_v<SampleType, double>,
                   "Oscillator supports float or double");

    void prepare (SampleType newSampleRate) noexcept
    {
        sampleRate = std::isfinite (newSampleRate) && newSampleRate > 0.0f
            ? newSampleRate : SampleType {};
        frequency = SampleType {};
        phaseIncrement = SampleType {};
        waveform = OscillatorWaveform::saw;
        shape = SampleType {};
        reset();
    }

    void reset (SampleType newPhase = SampleType {}) noexcept
    {
        phase = std::isfinite (newPhase)
            ? newPhase - std::floor (newPhase) : SampleType {};

        if (phase < SampleType {})
            phase += static_cast<SampleType> (1.0);

        cycleIndex = 0;
        downsampler.reset();
    }

    void setWaveform (OscillatorWaveform nextWaveform) noexcept
    {
        if (waveform != nextWaveform)
            downsampler.reset();

        waveform = nextWaveform;
    }

    void setFrequency (SampleType newFrequency) noexcept
    {
        if (! std::isfinite (newFrequency))
            newFrequency = newFrequency > 0.0f && sampleRate > 0.0f
                ? sampleRate * static_cast<SampleType> (0.49) : SampleType {};

        frequency = sampleRate > SampleType {}
            ? std::clamp (newFrequency, SampleType {},
                          sampleRate * static_cast<SampleType> (0.49))
            : SampleType {};
        phaseIncrement = sampleRate > SampleType {}
            ? frequency / (sampleRate * static_cast<SampleType> (4.0))
            : SampleType {};
    }

    void setShape (SampleType nextShape) noexcept
    {
        shape = std::isfinite (nextShape)
            ? std::clamp (nextShape, SampleType {},
                          static_cast<SampleType> (1.0))
            : SampleType {};
    }

    OscillatorWaveform getWaveform() const noexcept { return waveform; }
    SampleType getFrequency() const noexcept { return frequency; }
    SampleType getShape() const noexcept { return shape; }

    SampleType next() noexcept
    {
        if (phaseIncrement <= 0.0f)
            return SampleType {};

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

        if (phase >= static_cast<SampleType> (1.0))
        {
            phase -= static_cast<SampleType> (1.0);
            ++cycleIndex;
        }
    }

    SampleType renderAt (SampleType currentPhase) const noexcept
    {
        switch (waveform)
        {
            case OscillatorWaveform::saw:
                return renderSaw (currentPhase,
                                  shape * static_cast<SampleType> (2.0));
            case OscillatorWaveform::ramp:
                return renderSaw (currentPhase,
                                  static_cast<SampleType> (2.0)
                                      + shape * static_cast<SampleType> (2.0));
            case OscillatorWaveform::pulse:
                return currentPhase < pulseWidth()
                    ? static_cast<SampleType> (1.0)
                    : static_cast<SampleType> (-1.0);
            case OscillatorWaveform::triangle:
                return mirrorFold (rawTriangle (currentPhase) * foldDrive());
            case OscillatorWaveform::sine:
                return mirrorFold (std::sin (currentPhase * twoPi)
                                   * foldDrive());
        }

        return SampleType {};
    }

    SampleType renderSaw (SampleType currentPhase,
                          SampleType sawShape) const noexcept
    {
        const auto one = static_cast<SampleType> (1.0);
        const auto two = static_cast<SampleType> (2.0);
        const auto three = static_cast<SampleType> (3.0);
        const auto four = static_cast<SampleType> (4.0);
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
        const auto polarity = sawShape < two
            ? cyclePolarity : -cyclePolarity;
        const auto folded = polarity * saw < -tau ? -saw : saw;
        return (two * folded - polarity * (one - tau)) / (one + tau);
    }

    SampleType pulseWidth() const noexcept
    {
        constexpr auto edgeGuard = static_cast<SampleType> (0.02);
        return edgeGuard + shape * (static_cast<SampleType> (1.0)
                                    - static_cast<SampleType> (2.0) * edgeGuard);
    }

    SampleType foldDrive() const noexcept
    {
        return static_cast<SampleType> (1.0)
             + shape * static_cast<SampleType> (3.0);
    }

    static SampleType rawTriangle (SampleType currentPhase) noexcept
    {
        return currentPhase < static_cast<SampleType> (0.5)
            ? currentPhase * static_cast<SampleType> (4.0)
                - static_cast<SampleType> (1.0)
            : static_cast<SampleType> (3.0)
                - currentPhase * static_cast<SampleType> (4.0);
    }

    static SampleType mirrorFold (SampleType value) noexcept
    {
        auto folded = value;

        if (folded > static_cast<SampleType> (1.0))
        {
            folded = static_cast<SampleType> (2.0) - folded;

            if (folded < static_cast<SampleType> (-1.0))
                folded = static_cast<SampleType> (-2.0) - folded;
        }
        else if (folded < static_cast<SampleType> (-1.0))
        {
            folded = static_cast<SampleType> (-2.0) - folded;

            if (folded > static_cast<SampleType> (1.0))
                folded = static_cast<SampleType> (2.0) - folded;
        }

        return folded;
    }

    Downsampler4x<SampleType> downsampler;
    OscillatorWaveform waveform = OscillatorWaveform::saw;
    SampleType sampleRate {};
    SampleType frequency {};
    SampleType phase {};
    SampleType phaseIncrement {};
    SampleType shape {};
    int cycleIndex = 0;
};

} // namespace chardsp
