#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "chardsp_ADSREnvelope.h"
#include "chardsp_ColoredNoise.h"
#include "chardsp_Oscillator.h"
#include "chardsp_StateVariableFilter.h"

namespace chardsp
{

enum class OscillatorPhaseMode
{
    freeRunning,
    resetOnNote
};

// A small, allocation-free subtractive-synth voice: two oscillators plus
// colored noise, then ADSR and a state-variable filter. Pitch is expressed as
// a fractional semitone note number, keeping input protocols outside the DSP.
template <typename SampleType>
struct BasicSynthVoice
{
    static_assert (std::is_same_v<SampleType, float>
                   || std::is_same_v<SampleType, double>,
                   "BasicSynthVoice supports float or double");

    void prepare (SampleType newSampleRate, size_t voiceIndex = 0) noexcept
    {
        sampleRate = std::isfinite (newSampleRate)
                     && newSampleRate > SampleType {}
            ? newSampleRate : static_cast<SampleType> (48000);
        velocity = SampleType {};
        oscillator1Level = static_cast<SampleType> (0.7);
        oscillator2Level = static_cast<SampleType> (0.3);
        noiseLevel = SampleType {};
        baseFrequency = SampleType {};
        phaseMode = OscillatorPhaseMode::freeRunning;

        auto seed = 0x5f356495u
                  + static_cast<uint32_t> (voiceIndex + 1u) * 1103515245u;
        oscillator1.prepare (sampleRate);
        oscillator2.prepare (sampleRate);
        oscillator1.reset (nextRandomUnipolar (seed));
        oscillator2.reset (nextRandomUnipolar (seed));
        imperfectionDirection = nextRandomUnipolar (seed)
            < static_cast<SampleType> (0.5) ? static_cast<SampleType> (-1)
                                            : static_cast<SampleType> (1);
        imperfectionAmount = static_cast<SampleType> (0.5)
                           + static_cast<SampleType> (0.5)
                           * nextRandomUnipolar (seed);
        setPitchImperfection (static_cast<SampleType> (0.4));

        envelope.prepare (sampleRate);
        envelope.setShape (EnvelopeShape::exponential);
        envelope.setAttack (static_cast<SampleType> (0.008));
        envelope.setDecay (static_cast<SampleType> (0.18));
        envelope.setSustain (static_cast<SampleType> (0.62));
        envelope.setRelease (static_cast<SampleType> (0.6));

        filter.prepare (sampleRate);
        filter.setMode (FilterMode::lowpass);
        filter.setCutoffAndQ (static_cast<SampleType> (4000),
                              static_cast<SampleType> (0.8));
        noise.reset (0x12345678u
                     + static_cast<uint32_t> (voiceIndex) * 7919u);
        noise.setColor (NoiseColor::white);
    }

    void start (SampleType pitch, SampleType nextVelocity) noexcept
    {
        setPitch (pitch);
        filter.reset();

        if (phaseMode == OscillatorPhaseMode::resetOnNote)
        {
            oscillator1.reset();
            oscillator2.reset();
        }

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
        updateOscillatorFrequencies();
    }

    void setVelocity (SampleType nextVelocity) noexcept
    {
        velocity = std::clamp (nextVelocity, SampleType {},
                               static_cast<SampleType> (1));
    }

    void setOscillator1Waveform (OscillatorWaveform waveform) noexcept
    {
        oscillator1.setWaveform (waveform);
    }

    void setOscillator2Waveform (OscillatorWaveform waveform) noexcept
    {
        oscillator2.setWaveform (waveform);
    }

    void setOscillator1Shape (SampleType shape) noexcept
    {
        oscillator1.setShape (shape);
    }

    void setOscillator2Shape (SampleType shape) noexcept
    {
        oscillator2.setShape (shape);
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

    void setPitchImperfection (SampleType maximumSeparationCents) noexcept
    {
        const auto amount = std::isfinite (maximumSeparationCents)
            ? std::max (SampleType {}, maximumSeparationCents) : SampleType {};
        const auto halfSeparation = static_cast<SampleType> (0.5) * amount
                                  * imperfectionAmount * imperfectionDirection;
        oscillator1DetuneRatio = centsToRatio (-halfSeparation);
        oscillator2DetuneRatio = centsToRatio (halfSeparation);
        updateOscillatorFrequencies();
    }

    void setPhaseMode (OscillatorPhaseMode mode) noexcept { phaseMode = mode; }

    void setFilter (FilterMode mode, SampleType cutoff,
                    SampleType q) noexcept
    {
        filter.setMode (mode);
        filter.setCutoffAndQ (cutoff, q);
    }

    void setNoiseLevel (SampleType amount) noexcept
    {
        noiseLevel = std::clamp (amount, SampleType {},
                                 static_cast<SampleType> (1));
    }

    void setNoiseColor (NoiseColor color) noexcept { noise.setColor (color); }
    void setEnvelopeShape (EnvelopeShape shape) noexcept
    {
        envelope.setShape (shape);
    }

    void setAttack (SampleType seconds) noexcept { envelope.setAttack (seconds); }
    void setDecay (SampleType seconds) noexcept { envelope.setDecay (seconds); }
    void setSustain (SampleType level) noexcept { envelope.setSustain (level); }
    void setRelease (SampleType seconds) noexcept { envelope.setRelease (seconds); }

    SampleType next() noexcept
    {
        if (! envelope.isActive())
        {
            advanceFreeRunningOscillators();
            return SampleType {};
        }

        const auto amplitude = envelope.next();

        if (! envelope.isActive())
        {
            advanceFreeRunningOscillators();
            return SampleType {};
        }

        const auto excitation = oscillator1Level * oscillator1.next()
                              + oscillator2Level * oscillator2.next()
                              + noiseLevel * noise.next();
        return filter.process (velocity * amplitude * excitation);
    }

private:
    static uint32_t nextRandom (uint32_t& seed) noexcept
    {
        seed = seed * 1664525u + 1013904223u;
        return seed;
    }

    static SampleType nextRandomUnipolar (uint32_t& seed) noexcept
    {
        return static_cast<SampleType> (nextRandom (seed) >> 8u)
             * (static_cast<SampleType> (1) / static_cast<SampleType> (16777216));
    }

    static SampleType centsToRatio (SampleType cents) noexcept
    {
        return std::exp2 (cents / static_cast<SampleType> (1200));
    }

    void updateOscillatorFrequencies() noexcept
    {
        oscillator1.setFrequency (baseFrequency * oscillator1DetuneRatio);
        oscillator2.setFrequency (baseFrequency * oscillator2DetuneRatio);
    }

    void advanceFreeRunningOscillators() noexcept
    {
        if (phaseMode == OscillatorPhaseMode::freeRunning)
        {
            oscillator1.advance();
            oscillator2.advance();
        }
    }

    Oscillator<SampleType> oscillator1;
    Oscillator<SampleType> oscillator2;
    ADSREnvelope<SampleType> envelope;
    StateVariableFilter<SampleType> filter;
    ColoredNoise<SampleType> noise;
    SampleType sampleRate = static_cast<SampleType> (48000);
    SampleType velocity {};
    SampleType oscillator1Level {};
    SampleType oscillator2Level {};
    SampleType noiseLevel {};
    SampleType baseFrequency {};
    SampleType oscillator1DetuneRatio = static_cast<SampleType> (1);
    SampleType oscillator2DetuneRatio = static_cast<SampleType> (1);
    SampleType imperfectionDirection = static_cast<SampleType> (1);
    SampleType imperfectionAmount = static_cast<SampleType> (1);
    OscillatorPhaseMode phaseMode = OscillatorPhaseMode::freeRunning;
};

} // namespace chardsp
