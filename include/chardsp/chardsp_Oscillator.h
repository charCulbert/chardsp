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

enum class SawDirection
{
    descending,
    ascending
};

namespace oscillator_detail
{

template <typename SampleType>
struct PhaseState
{
    static_assert (std::is_same_v<SampleType, float>
                   || std::is_same_v<SampleType, double>,
                   "Oscillators support float or double");

    void prepare (SampleType newSampleRate) noexcept
    {
        CHOC_ASSERT (std::isfinite (newSampleRate)
                     && newSampleRate > SampleType {});
        sampleRate = std::isfinite (newSampleRate)
                     && newSampleRate > SampleType {}
            ? newSampleRate : SampleType {};
        frequency = SampleType {};
        phaseIncrement = SampleType {};
        reset();
    }

    void reset (SampleType newPhase = SampleType {}) noexcept
    {
        phase = std::isfinite (newPhase)
            ? newPhase - std::floor (newPhase) : SampleType {};

        if (phase < SampleType {})
            phase += static_cast<SampleType> (1);
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
            ? frequency / sampleRate : SampleType {};
    }

    SampleType advance() noexcept
    {
        phase += phaseIncrement;

        if (phase >= static_cast<SampleType> (1))
            phase -= static_cast<SampleType> (1);

        return phase;
    }

    SampleType sampleRate {};
    SampleType frequency {};
    SampleType phase {};
    SampleType phaseIncrement {};
};

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

template <typename SampleType>
struct EllipticState
{
    void prepare (SampleType sampleRate) noexcept
    {
        phase.prepare (sampleRate);

        if (phase.sampleRate > SampleType {})
            blep.emplace (phase.sampleRate);
        else
            blep.reset();

        allpass.reset();
    }

    void reset (SampleType newPhase = SampleType {}) noexcept
    {
        phase.reset (newPhase);

        if (blep)
            blep->reset();

        allpass.reset();
    }

    void setFrequency (SampleType frequency) noexcept
    {
        phase.setFrequency (frequency);
    }

    SampleType getFrequency() const noexcept { return phase.frequency; }

    PhaseState<SampleType> phase;
    std::optional<signalsmith::blep::EllipticBlep<SampleType>> blep;
    signalsmith::blep::EllipticBlepAllpass<SampleType> allpass;
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

        if (! state.blep || phase.phaseIncrement <= SampleType {})
            return SampleType {};

        const auto endPhase = phase.phase + phase.phaseIncrement;
        state.blep->step();
        phase.phase = endPhase;

        if (phase.phase >= static_cast<SampleType> (1))
        {
            const auto samplesInPast = (phase.phase - static_cast<SampleType> (1))
                                     / phase.phaseIncrement;
            phase.phase -= static_cast<SampleType> (1);
            const auto step = direction == SawDirection::ascending
                ? static_cast<SampleType> (-2) : static_cast<SampleType> (2);
            state.blep->add (step, 1, samplesInPast);
        }

        return state.allpass (oscillator_detail::saw (phase.phase, direction)
                              + state.blep->get());
    }

private:
    oscillator_detail::EllipticState<SampleType> state;
    SawDirection direction = SawDirection::descending;
};

template <typename SampleType>
struct EllipticPulseOscillator
{
    void prepare (SampleType sampleRate) noexcept { state.prepare (sampleRate); }
    void reset (SampleType phase = SampleType {}) noexcept { state.reset (phase); }
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

        if (! state.blep || phase.phaseIncrement <= SampleType {})
            return SampleType {};

        const auto startPhase = phase.phase;
        const auto endPhase = startPhase + phase.phaseIncrement;
        state.blep->step();

        if (startPhase < pulseWidth && endPhase >= pulseWidth)
            state.blep->add (static_cast<SampleType> (-2), 1,
                             (endPhase - pulseWidth) / phase.phaseIncrement);

        if (endPhase >= static_cast<SampleType> (1))
        {
            state.blep->add (static_cast<SampleType> (2), 1,
                             (endPhase - static_cast<SampleType> (1))
                                 / phase.phaseIncrement);

            const auto wrappedEnd = endPhase - static_cast<SampleType> (1);

            if (wrappedEnd >= pulseWidth)
                state.blep->add (static_cast<SampleType> (-2), 1,
                                 (wrappedEnd - pulseWidth)
                                     / phase.phaseIncrement);

            phase.phase = wrappedEnd;
        }
        else
        {
            phase.phase = endPhase;
        }

        const auto naive = phase.phase < pulseWidth
            ? static_cast<SampleType> (1) : static_cast<SampleType> (-1);
        return state.allpass (naive + state.blep->get());
    }

private:
    oscillator_detail::EllipticState<SampleType> state;
    SampleType pulseWidth = static_cast<SampleType> (0.5);
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

        if (! state.blep || phase.phaseIncrement <= SampleType {})
            return SampleType {};

        const auto startPhase = phase.phase;
        const auto endPhase = startPhase + phase.phaseIncrement;
        const auto slopeJump = static_cast<SampleType> (8)
                             * phase.phaseIncrement;
        state.blep->step();

        if (startPhase < static_cast<SampleType> (0.5)
            && endPhase >= static_cast<SampleType> (0.5))
            state.blep->add (-slopeJump, 2,
                             (endPhase - static_cast<SampleType> (0.5))
                                 / phase.phaseIncrement);

        if (endPhase >= static_cast<SampleType> (1))
        {
            state.blep->add (slopeJump, 2,
                             (endPhase - static_cast<SampleType> (1))
                                 / phase.phaseIncrement);
            phase.phase = endPhase - static_cast<SampleType> (1);
        }
        else
        {
            phase.phase = endPhase;
        }

        return state.allpass (oscillator_detail::triangle (phase.phase)
                              + state.blep->get());
    }

private:
    oscillator_detail::EllipticState<SampleType> state;
};

} // namespace chardsp
