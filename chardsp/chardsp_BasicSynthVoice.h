#pragma once

#include "chardsp/chardsp_ADSREnvelope.h"
#include "chardsp/chardsp_ColoredNoise.h"
#include "chardsp/chardsp_Oscillator.h"
#include "chardsp/chardsp_StateVariableFilter.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace chardsp
{

enum class BasicSynthWaveform
{
    saw,
    pulse,
    triangle,
    sine
};

namespace basic_synth_detail
{

template <typename SampleType>
struct Oscillator
{
    void prepare (SampleType sampleRate) noexcept { phase.prepare (sampleRate); }
    void reset() noexcept { phase.reset(); }
    void setFrequency (SampleType frequency) noexcept
    {
        phase.setFrequency (frequency);
    }

    void setWaveform (BasicSynthWaveform nextWaveform) noexcept
    {
        waveform = nextWaveform;
    }

    BasicSynthWaveform getWaveform() const noexcept { return waveform; }

    void setPulseWidth (SampleType width) noexcept
    {
        pulseWidth = oscillator_detail::sanitisePulseWidth (width);
    }

    SampleType getPulseWidth() const noexcept { return pulseWidth; }

    SampleType next() noexcept
    {
        if (phase.phaseIncrement <= SampleType {})
            return SampleType {};

        const auto currentPhase = phase.advance();

        switch (waveform)
        {
            case BasicSynthWaveform::saw:
                return oscillator_detail::saw (
                           currentPhase, SawDirection::descending)
                     + oscillator_detail::polyBlep (
                           currentPhase, phase.phaseIncrement);

            case BasicSynthWaveform::pulse:
            {
                const auto shifted = oscillator_detail::shiftedPhase (
                    currentPhase, pulseWidth);
                return (currentPhase < pulseWidth
                            ? static_cast<SampleType> (1)
                            : static_cast<SampleType> (-1))
                     + oscillator_detail::polyBlep (
                           currentPhase, phase.phaseIncrement)
                     - oscillator_detail::polyBlep (
                           shifted, phase.phaseIncrement);
            }

            case BasicSynthWaveform::triangle:
            {
                const auto corner = oscillator_detail::shiftedPhase (
                    currentPhase, static_cast<SampleType> (0.5));
                const auto slopeJump = static_cast<SampleType> (8)
                                     * phase.phaseIncrement;
                return oscillator_detail::triangle (currentPhase)
                     + slopeJump * oscillator_detail::polyBlamp (
                           currentPhase, phase.phaseIncrement)
                     - slopeJump * oscillator_detail::polyBlamp (
                           corner, phase.phaseIncrement);
            }

            case BasicSynthWaveform::sine:
                return std::sin (currentPhase * static_cast<SampleType> (
                    6.28318530717958647692));
        }

        return SampleType {};
    }

private:
    OscillatorPhase<SampleType> phase;
    BasicSynthWaveform waveform = BasicSynthWaveform::saw;
    SampleType pulseWidth = static_cast<SampleType> (0.5);
};

} // namespace basic_synth_detail

// Small subtractive voice: two selectable generic oscillators, optional
// coloured noise, ADSR, and SVF. Polyphony and note policy belong to VoicePool
// callers.
template <typename SampleType>
struct BasicSynthVoice
{
    static_assert (std::is_same_v<SampleType, float>
                   || std::is_same_v<SampleType, double>,
                   "BasicSynthVoice supports float or double");

    void prepare (SampleType newSampleRate, size_t voiceIndex = 0) noexcept
    {
        sampleRate = std::isfinite (newSampleRate) && newSampleRate > 0
            ? newSampleRate : static_cast<SampleType> (48000);
        velocity = SampleType {};
        velocitySensitivity = static_cast<SampleType> (1);
        velocityCurve = static_cast<SampleType> (1);
        oscillator1Level = static_cast<SampleType> (0.8);
        oscillator2Level = static_cast<SampleType> (0.2);
        noiseLevel = SampleType {};
        baseFrequency = SampleType {};

        oscillator1.prepare (sampleRate);
        oscillator2.prepare (sampleRate);
        oscillator1.setWaveform (BasicSynthWaveform::saw);
        oscillator2.setWaveform (BasicSynthWaveform::sine);
        oscillator1.setPulseWidth (static_cast<SampleType> (0.5));
        oscillator2.setPulseWidth (static_cast<SampleType> (0.5));
        oscillator1.reset();
        oscillator2.reset();

        envelope.prepare (sampleRate);
        envelope.setShape (chardsp::EnvelopeShape::exponential);
        envelope.setAttack (static_cast<SampleType> (0.008));
        envelope.setDecay (static_cast<SampleType> (0.18));
        envelope.setSustain (static_cast<SampleType> (0.62));
        envelope.setRelease (static_cast<SampleType> (0.6));

        filter.prepare (sampleRate);
        filter.setMode (chardsp::FilterMode::lowpass);
        filter.setCutoffAndQ (static_cast<SampleType> (4000),
                              static_cast<SampleType> (0.8));
        noise.reset (0x12345678u + static_cast<uint32_t> (voiceIndex) * 7919u);
        noise.setColor (chardsp::NoiseColor::white);
        resetState();
    }

    void start (SampleType pitch, SampleType nextVelocity) noexcept
    {
        setPitch (pitch);
        oscillator1.reset();
        oscillator2.reset();
        setVelocity (nextVelocity);
        envelope.trigger();
    }

    void release() noexcept { envelope.release(); }
    void forceRelease (SampleType seconds) noexcept
    {
        envelope.forceRelease (seconds);
    }

    bool isAvailable() const noexcept { return ! envelope.isActive(); }

    void setPitch (SampleType noteNumber) noexcept
    {
        baseFrequency = static_cast<SampleType> (440)
            * std::exp2 ((noteNumber - static_cast<SampleType> (69))
                         / static_cast<SampleType> (12));
        oscillator1.setFrequency (baseFrequency);
        oscillator2.setFrequency (baseFrequency);
    }

    void setVelocity (SampleType nextVelocity) noexcept
    {
        velocity = std::clamp (nextVelocity, SampleType {},
                               static_cast<SampleType> (1));
        updateVelocityGain();
    }

    void setVelocitySensitivity (SampleType amount) noexcept
    {
        velocitySensitivity = std::isfinite (amount)
            ? std::clamp (amount, SampleType {}, static_cast<SampleType> (1))
            : static_cast<SampleType> (1);
        updateVelocityGain();
    }

    void setVelocityCurve (SampleType exponent) noexcept
    {
        velocityCurve = std::isfinite (exponent)
            ? std::clamp (exponent, static_cast<SampleType> (0.01),
                          static_cast<SampleType> (8))
            : static_cast<SampleType> (1);
        updateVelocityGain();
    }

    SampleType getVelocitySensitivity() const noexcept
    {
        return velocitySensitivity;
    }

    SampleType getVelocityCurve() const noexcept { return velocityCurve; }

    void setOscillator1Waveform (BasicSynthWaveform waveform) noexcept
    {
        oscillator1.setWaveform (waveform);
    }

    void setOscillator2Waveform (BasicSynthWaveform waveform) noexcept
    {
        oscillator2.setWaveform (waveform);
    }

    BasicSynthWaveform getOscillator1Waveform() const noexcept
    {
        return oscillator1.getWaveform();
    }

    BasicSynthWaveform getOscillator2Waveform() const noexcept
    {
        return oscillator2.getWaveform();
    }

    void setOscillator1PulseWidth (SampleType width) noexcept
    {
        oscillator1.setPulseWidth (width);
    }

    void setOscillator2PulseWidth (SampleType width) noexcept
    {
        oscillator2.setPulseWidth (width);
    }

    SampleType getOscillator1PulseWidth() const noexcept
    {
        return oscillator1.getPulseWidth();
    }

    SampleType getOscillator2PulseWidth() const noexcept
    {
        return oscillator2.getPulseWidth();
    }

    void setOscillator1Level (SampleType level) noexcept
    {
        oscillator1Level = std::clamp (level, SampleType {},
                                       static_cast<SampleType> (1));
    }

    void setOscillator2Level (SampleType level) noexcept
    {
        oscillator2Level = std::clamp (level, SampleType {},
                                       static_cast<SampleType> (1));
    }

    void setNoiseLevel (SampleType amount) noexcept
    {
        noiseLevel = std::clamp (amount, SampleType {},
                                 static_cast<SampleType> (1));
    }

    void setNoiseColor (chardsp::NoiseColor color) noexcept
    {
        noise.setColor (color);
    }

    void setEnvelopeShape (chardsp::EnvelopeShape shape) noexcept
    {
        envelope.setShape (shape);
    }

    void setAttack (SampleType seconds) noexcept { envelope.setAttack (seconds); }
    void setDecay (SampleType seconds) noexcept { envelope.setDecay (seconds); }
    void setSustain (SampleType level) noexcept { envelope.setSustain (level); }
    void setRelease (SampleType seconds) noexcept { envelope.setRelease (seconds); }

    void setFilter (chardsp::FilterMode mode, SampleType cutoff,
                    SampleType q) noexcept
    {
        filter.setMode (mode);
        filter.setCutoffAndQ (cutoff, q);
    }

    SampleType next() noexcept
    {
        if (! envelope.isActive())
            return SampleType {};

        const auto envelopeLevel = envelope.next();
        if (! envelope.isActive())
            return SampleType {};

        const auto excitation = oscillator1Level * oscillator1.next()
                              + oscillator2Level * oscillator2.next()
                              + noiseLevel * noise.next();
        return filter.process (velocityGain * envelopeLevel * excitation);
    }

private:
    void resetState() noexcept
    {
        baseFrequency = SampleType {};
        velocityGain = SampleType {};
    }

    void updateVelocityGain() noexcept
    {
        const auto curved = std::pow (velocity, velocityCurve);
        velocityGain = static_cast<SampleType> (1)
                     + velocitySensitivity
                         * (curved - static_cast<SampleType> (1));
    }

    basic_synth_detail::Oscillator<SampleType> oscillator1;
    basic_synth_detail::Oscillator<SampleType> oscillator2;
    chardsp::ADSREnvelope<SampleType> envelope;
    chardsp::StateVariableFilter<SampleType> filter;
    chardsp::ColoredNoise<SampleType> noise;
    SampleType sampleRate = static_cast<SampleType> (48000);
    SampleType velocity {};
    SampleType velocitySensitivity = static_cast<SampleType> (1);
    SampleType velocityCurve = static_cast<SampleType> (1);
    SampleType velocityGain {};
    SampleType oscillator1Level = static_cast<SampleType> (0.8);
    SampleType oscillator2Level = static_cast<SampleType> (0.2);
    SampleType noiseLevel {};
    SampleType baseFrequency {};
};

} // namespace chardsp
