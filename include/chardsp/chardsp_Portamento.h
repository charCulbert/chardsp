#pragma once

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace chardsp
{

// Nomad-style portamento in semitone pitch space. A time/rate morph of zero
// gives constant-time glides; one gives a constant time per octave.
template <typename SampleType>
struct Portamento
{
    static_assert (std::is_same_v<SampleType, float>
                   || std::is_same_v<SampleType, double>,
                   "Portamento supports float or double");

    void prepare (SampleType newSampleRate) noexcept
    {
        sampleRate = sanitisePositive (newSampleRate,
                                       static_cast<SampleType> (48000));
    }

    void reset (SampleType pitch = static_cast<SampleType> (69)) noexcept
    {
        currentPitch = sanitisePitch (pitch);
        startPitch = currentPitch;
        targetPitch = currentPitch;
        glidePosition = static_cast<SampleType> (1);
        glideSamples = static_cast<SampleType> (1);
    }

    void setTime (SampleType seconds) noexcept
    {
        glideTime = sanitiseNonNegative (seconds);
    }

    void setShape (SampleType nextShape) noexcept
    {
        shape = sanitiseUnit (nextShape);
    }

    void setTimeRateMorph (SampleType morph) noexcept
    {
        timeRateMorph = sanitiseUnit (morph);
    }

    SampleType getTime() const noexcept { return glideTime; }
    SampleType getShape() const noexcept { return shape; }
    SampleType getTimeRateMorph() const noexcept { return timeRateMorph; }

    void setTarget (SampleType pitch, bool shouldGlide) noexcept
    {
        targetPitch = sanitisePitch (pitch);

        if (shouldGlide && glideTime > SampleType {})
        {
            startPitch = currentPitch;
            const auto octaves = std::abs (targetPitch - startPitch)
                               / static_cast<SampleType> (12);
            const auto duration = glideTime
                * ((static_cast<SampleType> (1) - timeRateMorph)
                   + timeRateMorph * octaves);
            glideSamples = std::max (static_cast<SampleType> (1),
                                     duration * sampleRate);
            glidePosition = SampleType {};
        }
        else
        {
            currentPitch = targetPitch;
            startPitch = targetPitch;
            glidePosition = static_cast<SampleType> (1);
            glideSamples = static_cast<SampleType> (1);
        }
    }

    SampleType next() noexcept
    {
        if (glidePosition < glideSamples)
        {
            const auto t = std::clamp (glidePosition / glideSamples,
                                       SampleType {},
                                       static_cast<SampleType> (1));
            const auto analogStrength = static_cast<SampleType> (1)
                                      + shape * static_cast<SampleType> (5);
            const auto analog = (static_cast<SampleType> (1)
                                 - std::exp (-analogStrength * t))
                              / (static_cast<SampleType> (1)
                                 - std::exp (-analogStrength));
            const auto curved = t + (analog - t) * shape;
            currentPitch = startPitch
                         + (targetPitch - startPitch) * curved;
            glidePosition += static_cast<SampleType> (1);
        }
        else
        {
            currentPitch = targetPitch;
        }

        return currentPitch;
    }

    SampleType getCurrent() const noexcept { return currentPitch; }
    SampleType getTarget() const noexcept { return targetPitch; }
    bool isGliding() const noexcept { return glidePosition < glideSamples; }

private:
    static SampleType sanitisePositive (SampleType value,
                                        SampleType fallback) noexcept
    {
        return std::isfinite (value) && value > SampleType {} ? value
                                                              : fallback;
    }

    static SampleType sanitiseNonNegative (SampleType value) noexcept
    {
        return std::isfinite (value) ? std::max (SampleType {}, value)
                                     : SampleType {};
    }

    static SampleType sanitiseUnit (SampleType value) noexcept
    {
        return std::isfinite (value)
            ? std::clamp (value, SampleType {}, static_cast<SampleType> (1))
            : SampleType {};
    }

    static SampleType sanitisePitch (SampleType value) noexcept
    {
        return std::isfinite (value) ? value : SampleType {};
    }

    SampleType sampleRate = static_cast<SampleType> (48000);
    SampleType glideTime = static_cast<SampleType> (0.08);
    SampleType shape {};
    SampleType timeRateMorph {};
    SampleType currentPitch = static_cast<SampleType> (69);
    SampleType startPitch = static_cast<SampleType> (69);
    SampleType targetPitch = static_cast<SampleType> (69);
    SampleType glidePosition = static_cast<SampleType> (1);
    SampleType glideSamples = static_cast<SampleType> (1);
};

} // namespace chardsp
