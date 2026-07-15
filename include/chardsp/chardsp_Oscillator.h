#pragma once

#include <algorithm>
#include <array>
#include <cmath>

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
struct Oscillator
{
    void prepare (float newSampleRate) noexcept
    {
        sampleRate = std::isfinite (newSampleRate) && newSampleRate > 0.0f
            ? newSampleRate : 0.0f;
        frequency = 0.0f;
        phaseIncrement = 0.0f;
        waveform = OscillatorWaveform::saw;
        shape = 0.0f;
        reset();
    }

    void reset (float newPhase = 0.0f) noexcept
    {
        phase = std::isfinite (newPhase)
            ? newPhase - std::floor (newPhase) : 0.0f;

        if (phase < 0.0f)
            phase += 1.0f;

        cycleIndex = 0;
        downsampler.reset();
    }

    void setWaveform (OscillatorWaveform nextWaveform) noexcept
    {
        if (waveform != nextWaveform)
            downsampler.reset();

        waveform = nextWaveform;
    }

    void setFrequency (float newFrequency) noexcept
    {
        if (! std::isfinite (newFrequency))
            newFrequency = newFrequency > 0.0f && sampleRate > 0.0f
                ? sampleRate * 0.49f : 0.0f;

        frequency = sampleRate > 0.0f
            ? std::clamp (newFrequency, 0.0f, sampleRate * 0.49f)
            : 0.0f;
        phaseIncrement = sampleRate > 0.0f
            ? frequency / (sampleRate * 4.0f) : 0.0f;
    }

    void setShape (float nextShape) noexcept
    {
        shape = std::isfinite (nextShape)
            ? std::clamp (nextShape, 0.0f, 1.0f) : 0.0f;
    }

    OscillatorWaveform getWaveform() const noexcept { return waveform; }
    float getFrequency() const noexcept { return frequency; }
    float getShape() const noexcept { return shape; }

    float next() noexcept
    {
        if (phaseIncrement <= 0.0f)
            return 0.0f;

        std::array<float, 4> oversampled {};

        for (auto& sample : oversampled)
        {
            sample = renderAt (phase);
            advancePhase();
        }

        return downsampler.process (oversampled);
    }

private:
    static constexpr float twoPi = 6.28318530717958647692f;

    void advancePhase() noexcept
    {
        phase += phaseIncrement;

        if (phase >= 1.0f)
        {
            phase -= 1.0f;
            ++cycleIndex;
        }
    }

    float renderAt (float currentPhase) const noexcept
    {
        switch (waveform)
        {
            case OscillatorWaveform::saw:
                return renderSaw (currentPhase, shape * 2.0f);
            case OscillatorWaveform::ramp:
                return renderSaw (currentPhase, 2.0f + shape * 2.0f);
            case OscillatorWaveform::pulse:
                return currentPhase < pulseWidth() ? 1.0f : -1.0f;
            case OscillatorWaveform::triangle:
                return mirrorFold (rawTriangle (currentPhase) * foldDrive());
            case OscillatorWaveform::sine:
                return mirrorFold (std::sin (currentPhase * twoPi)
                                   * foldDrive());
        }

        return 0.0f;
    }

    float renderSaw (float currentPhase, float sawShape) const noexcept
    {
        const auto foldDepth = sawShape < 1.0f ? sawShape
                             : sawShape < 2.0f ? 2.0f - sawShape
                             : sawShape < 3.0f ? sawShape - 2.0f
                                               : 4.0f - sawShape;
        const auto tau = 1.0f - foldDepth;
        const auto baseSign = sawShape < 1.0f || sawShape >= 3.0f
            ? 1.0f : -1.0f;
        const auto saw = baseSign * (1.0f - 2.0f * currentPhase);

        if (tau >= 0.999999f)
            return saw;

        const auto cyclePolarity = (cycleIndex & 1) == 0 ? -1.0f : 1.0f;
        const auto polarity = sawShape < 2.0f
            ? cyclePolarity : -cyclePolarity;
        const auto folded = polarity * saw < -tau ? -saw : saw;
        return (2.0f * folded - polarity * (1.0f - tau))
             / (1.0f + tau);
    }

    float pulseWidth() const noexcept
    {
        constexpr float edgeGuard = 0.02f;
        return edgeGuard + shape * (1.0f - 2.0f * edgeGuard);
    }

    float foldDrive() const noexcept
    {
        return 1.0f + shape * 3.0f;
    }

    static float rawTriangle (float currentPhase) noexcept
    {
        return currentPhase < 0.5f
            ? currentPhase * 4.0f - 1.0f
            : 3.0f - currentPhase * 4.0f;
    }

    static float mirrorFold (float value) noexcept
    {
        auto folded = value;

        if (folded > 1.0f)
        {
            folded = 2.0f - folded;

            if (folded < -1.0f)
                folded = -2.0f - folded;
        }
        else if (folded < -1.0f)
        {
            folded = -2.0f - folded;

            if (folded > 1.0f)
                folded = 2.0f - folded;
        }

        return folded;
    }

    Downsampler4x downsampler;
    OscillatorWaveform waveform = OscillatorWaveform::saw;
    float sampleRate = 0.0f;
    float frequency = 0.0f;
    float phase = 0.0f;
    float phaseIncrement = 0.0f;
    float shape = 0.0f;
    int cycleIndex = 0;
};

} // namespace chardsp
