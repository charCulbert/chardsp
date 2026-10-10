#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <type_traits>

#include "chardsp_OscillatorEvents.h"
#include "chardsp_OscillatorPhase.h"
#include "elliptic-blep.h"

namespace chardsp
{

enum class SawDirection
{
    descending,
    ascending
};

namespace oscillator_detail
{

template <typename SampleType>
using PhaseState = OscillatorPhase<SampleType>;

template <typename SampleType>
SampleType polyBlep (SampleType phase, SampleType increment) noexcept
{
    if (increment <= SampleType {})
        return SampleType {};

    if (phase < increment)
    {
        const auto t = phase / increment;
        return t + t - t * t - static_cast<SampleType> (1);
    }

    if (phase > static_cast<SampleType> (1) - increment)
    {
        const auto t = (phase - static_cast<SampleType> (1)) / increment;
        return t * t + t + t + static_cast<SampleType> (1);
    }

    return SampleType {};
}

// Integral of polyBlep in sample-time units. Multiplying this by a
// per-sample slope jump rounds a continuous waveform's sharp corner.
template <typename SampleType>
SampleType polyBlamp (SampleType phase, SampleType increment) noexcept
{
    if (increment <= SampleType {})
        return SampleType {};

    if (phase < increment)
    {
        const auto t = phase / increment - static_cast<SampleType> (1);
        return -(t * t * t) / static_cast<SampleType> (3);
    }

    if (phase > static_cast<SampleType> (1) - increment)
    {
        const auto t = (phase - static_cast<SampleType> (1)) / increment
                     + static_cast<SampleType> (1);
        return (t * t * t) / static_cast<SampleType> (3);
    }

    return SampleType {};
}

template <typename SampleType>
SampleType shiftedPhase (SampleType phase, SampleType offset) noexcept
{
    auto shifted = phase - offset;
    return shifted < SampleType {}
        ? shifted + static_cast<SampleType> (1) : shifted;
}

template <typename SampleType>
SampleType saw (SampleType phase, SawDirection direction) noexcept
{
    return direction == SawDirection::ascending
        ? static_cast<SampleType> (2) * phase - static_cast<SampleType> (1)
        : static_cast<SampleType> (1) - static_cast<SampleType> (2) * phase;
}

template <typename SampleType>
SampleType triangle (SampleType phase) noexcept
{
    return phase < static_cast<SampleType> (0.5)
        ? phase * static_cast<SampleType> (4) - static_cast<SampleType> (1)
        : static_cast<SampleType> (3) - phase * static_cast<SampleType> (4);
}

template <typename SampleType>
SampleType sanitisePulseWidth (SampleType width) noexcept
{
    return std::isfinite (width)
        ? std::clamp (width, static_cast<SampleType> (0.001),
                      static_cast<SampleType> (0.999))
        : static_cast<SampleType> (0.5);
}

// Signalsmith's elliptic BLEP and its allpass, for oscillators which schedule
// their own discontinuities. eventOffset is measured from the beginning of the
// current sample: 0 is the previous sample boundary and 1 is the current
// boundary. Until prepare() it passes samples through uncorrected.
//
// prepare() builds Signalsmith's lookup tables and is not an audio-thread
// operation. Every other method is allocation-free.
template <typename SampleType>
struct EllipticCorrection
{
    static_assert (std::is_same_v<SampleType, float>
                   || std::is_same_v<SampleType, double>,
                   "EllipticCorrection supports float or double");

    void prepare (SampleType sampleRate) noexcept
    {
        if (std::isfinite (sampleRate) && sampleRate > SampleType {})
            blep.emplace (sampleRate);
        else
            blep.reset();

        allpass.reset();
    }

    void reset() noexcept
    {
        if (blep)
            blep->reset();

        allpass.reset();
    }

    bool isPrepared() const noexcept { return blep.has_value(); }

    void beginSample() noexcept
    {
        if (blep)
            blep->step();
    }

    void addStep (SampleType eventOffset, SampleType height) noexcept
    {
        add (eventOffset, height, 1);
    }

    void addSlopeJump (SampleType eventOffset,
                       SampleType jumpPerSample) noexcept
    {
        add (eventOffset, jumpPerSample, 2);
    }

    SampleType process (SampleType uncorrectedSample) noexcept
    {
        if (! blep)
            return uncorrectedSample;

        return allpass (uncorrectedSample + blep->get());
    }

private:
    void add (SampleType eventOffset, SampleType amount,
              std::size_t order) noexcept
    {
        if (! blep || ! std::isfinite (eventOffset)
            || ! std::isfinite (amount) || amount == SampleType {})
            return;

        const auto offset = std::clamp (
            eventOffset, SampleType {}, static_cast<SampleType> (1));
        blep->add (amount, order,
                   static_cast<SampleType> (1) - offset);
    }

    std::optional<signalsmith::blep::EllipticBlep<SampleType>> blep;
    signalsmith::blep::EllipticBlepAllpass<SampleType> allpass;
};

template <typename SampleType>
struct EllipticState
{
    void prepare (SampleType sampleRate) noexcept
    {
        phase.prepare (sampleRate);
        correction.prepare (phase.sampleRate);
    }

    void reset (SampleType newPhase = SampleType {}) noexcept
    {
        phase.reset (newPhase);
        correction.reset();
    }

    void setFrequency (SampleType frequency) noexcept
    {
        phase.setFrequency (frequency);
    }

    SampleType getFrequency() const noexcept { return phase.frequency; }

    PhaseState<SampleType> phase;
    EllipticCorrection<SampleType> correction;
};

} // namespace oscillator_detail

template <typename SampleType>
struct NaiveSawOscillator
{
    void prepare (SampleType sampleRate) noexcept { state.prepare (sampleRate); }
    void reset (SampleType phase = SampleType {}) noexcept { state.reset (phase); }
    void setFrequency (SampleType frequency) noexcept { state.setFrequency (frequency); }
    SampleType getFrequency() const noexcept { return state.frequency; }
    void setDirection (SawDirection nextDirection) noexcept { direction = nextDirection; }
    SawDirection getDirection() const noexcept { return direction; }
    void advance() noexcept { state.advance(); }

    SampleType next() noexcept
    {
        if (state.phaseIncrement <= SampleType {})
            return SampleType {};

        return oscillator_detail::saw (state.advance(), direction);
    }

private:
    oscillator_detail::PhaseState<SampleType> state;
    SawDirection direction = SawDirection::descending;
};

template <typename SampleType>
struct NaivePulseOscillator
{
    void prepare (SampleType sampleRate) noexcept { state.prepare (sampleRate); }
    void reset (SampleType phase = SampleType {}) noexcept { state.reset (phase); }
    void setFrequency (SampleType frequency) noexcept { state.setFrequency (frequency); }
    SampleType getFrequency() const noexcept { return state.frequency; }
    void setPulseWidth (SampleType width) noexcept
    {
        pulseWidth = oscillator_detail::sanitisePulseWidth (width);
    }
    SampleType getPulseWidth() const noexcept { return pulseWidth; }
    void advance() noexcept { state.advance(); }

    SampleType next() noexcept
    {
        if (state.phaseIncrement <= SampleType {})
            return SampleType {};

        return state.advance() < pulseWidth
            ? static_cast<SampleType> (1) : static_cast<SampleType> (-1);
    }

private:
    oscillator_detail::PhaseState<SampleType> state;
    SampleType pulseWidth = static_cast<SampleType> (0.5);
};

template <typename SampleType>
struct NaiveTriangleOscillator
{
    void prepare (SampleType sampleRate) noexcept { state.prepare (sampleRate); }
    void reset (SampleType phase = SampleType {}) noexcept { state.reset (phase); }
    void setFrequency (SampleType frequency) noexcept { state.setFrequency (frequency); }
    SampleType getFrequency() const noexcept { return state.frequency; }
    void advance() noexcept { state.advance(); }

    SampleType next() noexcept
    {
        if (state.phaseIncrement <= SampleType {})
            return SampleType {};

        return oscillator_detail::triangle (state.advance());
    }

private:
    oscillator_detail::PhaseState<SampleType> state;
};

template <typename SampleType>
struct SineOscillator
{
    void prepare (SampleType sampleRate) noexcept { state.prepare (sampleRate); }
    void reset (SampleType phase = SampleType {}) noexcept { state.reset (phase); }
    void setFrequency (SampleType frequency) noexcept { state.setFrequency (frequency); }
    SampleType getFrequency() const noexcept { return state.frequency; }
    void advance() noexcept { state.advance(); }

    SampleType next() noexcept
    {
        if (state.phaseIncrement <= SampleType {})
            return SampleType {};

        return std::sin (state.advance() * static_cast<SampleType> (
            6.28318530717958647692));
    }

private:
    oscillator_detail::PhaseState<SampleType> state;
};

template <typename SampleType>
struct PolyBLEPSawOscillator
{
    void prepare (SampleType sampleRate) noexcept { state.prepare (sampleRate); }
    void reset (SampleType phase = SampleType {}) noexcept { state.reset (phase); }
    void setFrequency (SampleType frequency) noexcept { state.setFrequency (frequency); }
    SampleType getFrequency() const noexcept { return state.frequency; }
    void setDirection (SawDirection nextDirection) noexcept { direction = nextDirection; }
    SawDirection getDirection() const noexcept { return direction; }
    void advance() noexcept { state.advance(); }

    SampleType next() noexcept
    {
        if (state.phaseIncrement <= SampleType {})
            return SampleType {};

        const auto phase = state.advance();
        const auto correction = oscillator_detail::polyBlep (
            phase, state.phaseIncrement);
        return oscillator_detail::saw (phase, direction)
             + (direction == SawDirection::ascending ? -correction : correction);
    }

private:
    oscillator_detail::PhaseState<SampleType> state;
    SawDirection direction = SawDirection::descending;
};

template <typename SampleType>
struct PolyBLEPPulseOscillator
{
    void prepare (SampleType sampleRate) noexcept { state.prepare (sampleRate); }
    void reset (SampleType phase = SampleType {}) noexcept { state.reset (phase); }
    void setFrequency (SampleType frequency) noexcept { state.setFrequency (frequency); }
    SampleType getFrequency() const noexcept { return state.frequency; }
    void setPulseWidth (SampleType width) noexcept
    {
        pulseWidth = oscillator_detail::sanitisePulseWidth (width);
    }
    SampleType getPulseWidth() const noexcept { return pulseWidth; }
    void advance() noexcept { state.advance(); }

    SampleType next() noexcept
    {
        if (state.phaseIncrement <= SampleType {})
            return SampleType {};

        const auto phase = state.advance();
        const auto shifted = oscillator_detail::shiftedPhase (phase, pulseWidth);
        return (phase < pulseWidth ? static_cast<SampleType> (1)
                                   : static_cast<SampleType> (-1))
             + oscillator_detail::polyBlep (phase, state.phaseIncrement)
             - oscillator_detail::polyBlep (shifted, state.phaseIncrement);
    }

private:
    oscillator_detail::PhaseState<SampleType> state;
    SampleType pulseWidth = static_cast<SampleType> (0.5);
};

template <typename SampleType>
struct PolyBLEPTriangleOscillator
{
    void prepare (SampleType sampleRate) noexcept { state.prepare (sampleRate); }
    void reset (SampleType phase = SampleType {}) noexcept { state.reset (phase); }
    void setFrequency (SampleType frequency) noexcept { state.setFrequency (frequency); }
    SampleType getFrequency() const noexcept { return state.frequency; }
    void advance() noexcept { state.advance(); }

    SampleType next() noexcept
    {
        if (state.phaseIncrement <= SampleType {})
            return SampleType {};

        const auto phase = state.advance();
        const auto corner = oscillator_detail::shiftedPhase (
            phase, static_cast<SampleType> (0.5));
        const auto slopeJump = static_cast<SampleType> (8)
                             * state.phaseIncrement;
        return oscillator_detail::triangle (phase)
             + slopeJump * oscillator_detail::polyBlamp (
                   phase, state.phaseIncrement)
             - slopeJump * oscillator_detail::polyBlamp (
                   corner, state.phaseIncrement);
    }

private:
    oscillator_detail::PhaseState<SampleType> state;
};

template <typename SampleType>
struct EllipticSawOscillator
{
    void prepare (SampleType sampleRate) noexcept { state.prepare (sampleRate); }
    void reset (SampleType phase = SampleType {}) noexcept { state.reset (phase); }
    void setFrequency (SampleType frequency) noexcept { state.setFrequency (frequency); }
    SampleType getFrequency() const noexcept { return state.getFrequency(); }
    void setDirection (SawDirection nextDirection) noexcept { direction = nextDirection; }
    SawDirection getDirection() const noexcept { return direction; }
    void advance() noexcept { (void) next(); }

    SampleType next() noexcept
    {
        auto& phase = state.phase;

        if (! state.correction.isPrepared()
            || phase.phaseIncrement <= SampleType {})
            return SampleType {};

        const auto endPhase = phase.phase + phase.phaseIncrement;
        state.correction.beginSample();
        phase.phase = endPhase;

        if (phase.phase >= static_cast<SampleType> (1))
        {
            const auto samplesInPast = (phase.phase - static_cast<SampleType> (1))
                                     / phase.phaseIncrement;
            phase.phase -= static_cast<SampleType> (1);
            const auto step = direction == SawDirection::ascending
                ? static_cast<SampleType> (-2) : static_cast<SampleType> (2);
            state.correction.addStep (
                static_cast<SampleType> (1) - samplesInPast, step);
        }

        return state.correction.process (
            oscillator_detail::saw (phase.phase, direction));
    }

private:
    oscillator_detail::EllipticState<SampleType> state;
    SawDirection direction = SawDirection::descending;
};

template <typename SampleType>
struct EllipticPulseOscillator
{
    void prepare (SampleType sampleRate) noexcept
    {
        state.prepare (sampleRate);
        previousPulseWidth = pulseWidth;
    }

    void reset (SampleType phase = SampleType {}) noexcept
    {
        state.reset (phase);
        previousPulseWidth = pulseWidth;
    }
    void setFrequency (SampleType frequency) noexcept { state.setFrequency (frequency); }
    SampleType getFrequency() const noexcept { return state.getFrequency(); }
    void setPulseWidth (SampleType width) noexcept
    {
        pulseWidth = oscillator_detail::sanitisePulseWidth (width);
    }
    SampleType getPulseWidth() const noexcept { return pulseWidth; }
    void advance() noexcept { (void) next(); }

    SampleType next() noexcept
    {
        auto& phase = state.phase;

        if (! state.correction.isPrepared())
            return SampleType {};

        if (phase.phaseIncrement <= SampleType {})
        {
            previousPulseWidth = pulseWidth;
            return SampleType {};
        }

        const auto startPhase = phase.phase;
        const auto endPhase = startPhase + phase.phaseIncrement;
        state.correction.beginSample();

        forEachMovingPulseEdge (
            startPhase,
            endPhase,
            previousPulseWidth,
            pulseWidth,
            SampleType {},
            static_cast<SampleType> (1),
            [&] (PulseEdgeEvent<SampleType> event)
            {
                state.correction.addStep (event.offset, event.step);
            });

        if (endPhase >= static_cast<SampleType> (1))
        {
            phase.phase =
                endPhase - static_cast<SampleType> (1);
        }
        else
        {
            phase.phase = endPhase;
        }

        const auto naive = phase.phase < pulseWidth
            ? static_cast<SampleType> (1) : static_cast<SampleType> (-1);
        previousPulseWidth = pulseWidth;
        return state.correction.process (naive);
    }

private:
    oscillator_detail::EllipticState<SampleType> state;
    SampleType pulseWidth = static_cast<SampleType> (0.5);
    SampleType previousPulseWidth = static_cast<SampleType> (0.5);
};

template <typename SampleType>
struct EllipticTriangleOscillator
{
    void prepare (SampleType sampleRate) noexcept { state.prepare (sampleRate); }
    void reset (SampleType phase = SampleType {}) noexcept { state.reset (phase); }
    void setFrequency (SampleType frequency) noexcept { state.setFrequency (frequency); }
    SampleType getFrequency() const noexcept { return state.getFrequency(); }
    void advance() noexcept { (void) next(); }

    SampleType next() noexcept
    {
        auto& phase = state.phase;

        if (! state.correction.isPrepared()
            || phase.phaseIncrement <= SampleType {})
            return SampleType {};

        const auto startPhase = phase.phase;
        const auto endPhase = startPhase + phase.phaseIncrement;
        const auto slopeJump = static_cast<SampleType> (8)
                             * phase.phaseIncrement;
        state.correction.beginSample();

        if (startPhase < static_cast<SampleType> (0.5)
            && endPhase >= static_cast<SampleType> (0.5))
            state.correction.addSlopeJump (
                static_cast<SampleType> (1)
                    - (endPhase - static_cast<SampleType> (0.5))
                        / phase.phaseIncrement,
                -slopeJump);

        if (endPhase >= static_cast<SampleType> (1))
        {
            state.correction.addSlopeJump (
                static_cast<SampleType> (1)
                    - (endPhase - static_cast<SampleType> (1))
                        / phase.phaseIncrement,
                slopeJump);
            phase.phase = endPhase - static_cast<SampleType> (1);
        }
        else
        {
            phase.phase = endPhase;
        }

        return state.correction.process (
            oscillator_detail::triangle (phase.phase));
    }

private:
    oscillator_detail::EllipticState<SampleType> state;
};

} // namespace chardsp
