#pragma once

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace chardsp
{

enum class EnvelopeShape
{
    exponential,
    linear
};

// Finite-time ADSR with stages that may be edited while they are running.
template <typename SampleType>
struct ADSREnvelope
{
    static_assert (std::is_same_v<SampleType, float>
                   || std::is_same_v<SampleType, double>,
                   "ADSREnvelope supports float or double");

    void prepare (SampleType newSampleRate) noexcept
    {
        sampleRate = std::isfinite (newSampleRate) && newSampleRate > 0.0f
            ? newSampleRate
            : 48000.0f;
        attackFactor = secondsToExponentialFactor (attackSeconds, attackCompletionRatio);
        decayFactor = secondsToExponentialFactor (decaySeconds, envelopeCompletionRatio);
        releaseFactor = secondsToExponentialFactor (releaseSeconds, releaseCompletionRatio);
        reset();
    }

    void reset() noexcept
    {
        current = 0.0f;
        stageTarget = 0.0f;
        level = 0.0f;
        stageDifference = 0.0f;
        stageThreshold = minimumDistance;
        stageLinearStep = 0.0f;
        forcedReleaseStep = 0.0f;
        stageIsLinear = false;
        stage = Stage::idle;
    }

    void setAttack (SampleType seconds) noexcept
    {
        attackSeconds = sanitiseSeconds (seconds);
        attackFactor = secondsToExponentialFactor (attackSeconds, attackCompletionRatio);

        if (stage == Stage::attack)
            beginAttack();
    }

    void setDecay (SampleType seconds) noexcept
    {
        decaySeconds = sanitiseSeconds (seconds);
        decayFactor = secondsToExponentialFactor (decaySeconds, envelopeCompletionRatio);

        if (stage == Stage::decay)
            beginDecay();
    }

    void setSustain (SampleType nextLevel) noexcept
    {
        sustainLevel = std::isfinite (nextLevel)
            ? std::clamp (nextLevel, SampleType {}, static_cast<SampleType> (1.0))
            : SampleType {};

        if (stage == Stage::decay)
            beginDecay();
    }

    void setRelease (SampleType seconds) noexcept
    {
        releaseSeconds = sanitiseSeconds (seconds);
        releaseFactor = secondsToExponentialFactor (releaseSeconds, releaseCompletionRatio);

        if (stage == Stage::release)
            beginRelease();
    }

    void setShape (EnvelopeShape nextShape) noexcept
    {
        shape = nextShape;

        if (stage == Stage::attack)
            beginAttack();
        else if (stage == Stage::decay)
            beginDecay();
        else if (stage == Stage::release)
            beginRelease();
    }

    // Starts a new attack from zero.
    void trigger() noexcept
    {
        startFromLevel (0.0f);
    }

    // Starts a new attack without jumping away from the current level.
    void retrigger() noexcept
    {
        startFromLevel (current);
    }

    void release() noexcept
    {
        beginRelease();
    }

    // Forced release is always linear, independent of the envelope shape.
    void forceRelease (SampleType seconds) noexcept
    {
        level = 0.0f;
        stageTarget = 0.0f;
        stageDifference = current;
        stageThreshold = minimumDistance;
        seconds = sanitiseSeconds (seconds);

        if (seconds <= 0.0f || std::abs (stageDifference) <= stageThreshold)
        {
            current = 0.0f;
            stageDifference = 0.0f;
            stage = Stage::idle;
            return;
        }

        forcedReleaseStep = std::abs (stageDifference) * secondsToLinearRate (seconds);
        stage = Stage::forcedRelease;
    }

    SampleType next() noexcept
    {
        if (stage == Stage::decay && stageDifference == 0.0f)
            return current;

        if (stage == Stage::attack || stage == Stage::decay || stage == Stage::release)
        {
            advanceActiveStage();
            current = level + stageDifference;

            if (std::abs (stageDifference) < stageThreshold)
            {
                stageDifference = 0.0f;
                current = stageTarget;
                level = stageTarget;

                if (stage == Stage::attack)
                    beginDecay();
                else if (stage == Stage::release && std::abs (level) < 1.0e-6f)
                    stage = Stage::idle;
            }
        }
        else if (stage == Stage::forcedRelease)
        {
            if (stageDifference > 0.0f)
                stageDifference = std::max (SampleType {},
                                            stageDifference - forcedReleaseStep);
            else
                stageDifference = std::min (SampleType {},
                                            stageDifference + forcedReleaseStep);

            current = level + stageDifference;

            if (std::abs (stageDifference) <= stageThreshold)
            {
                current = 0.0f;
                stageDifference = 0.0f;
                stage = Stage::idle;
            }
        }
        else
        {
            current = 0.0f;
        }

        return current;
    }

    SampleType getCurrent() const noexcept { return current; }
    bool isActive() const noexcept { return stage != Stage::idle; }

private:
    enum class Stage
    {
        attack,
        decay,
        release,
        idle,
        forcedRelease
    };

    static constexpr SampleType envelopeCompletionRatio = static_cast<SampleType> (0.047);
    static constexpr SampleType attackCompletionRatio = static_cast<SampleType> (0.040);
    static constexpr SampleType releaseCompletionRatio = static_cast<SampleType> (0.049);
    static constexpr SampleType minimumDistance = static_cast<SampleType> (1.0e-7);

    static SampleType sanitiseSeconds (SampleType seconds) noexcept
    {
        return std::isfinite (seconds) ? std::max (SampleType {}, seconds)
                                       : SampleType {};
    }

    SampleType secondsToLinearRate (SampleType seconds) const noexcept
    {
        return static_cast<SampleType> (1.0)
             / std::max (static_cast<SampleType> (1.0), seconds * sampleRate);
    }

    SampleType secondsToExponentialFactor (SampleType seconds,
                                           SampleType completionRatio) const noexcept
    {
        const auto samples = std::max (static_cast<SampleType> (1.0),
                                       seconds * sampleRate);
        return std::clamp (std::pow (completionRatio,
                                     static_cast<SampleType> (1.0) / samples),
                           SampleType {},
                           static_cast<SampleType> (0.999999));
    }

    SampleType stageCompletionThreshold (SampleType completionRatio) const noexcept
    {
        return std::max (minimumDistance, std::abs (stageDifference) * completionRatio);
    }

    static SampleType finiteTimeAsymptote (SampleType startLevel,
                                           SampleType targetLevel,
                                           SampleType completionRatio) noexcept
    {
        const auto ratio = std::clamp (completionRatio,
                                       static_cast<SampleType> (0.000001),
                                       static_cast<SampleType> (0.95));
        return (targetLevel - startLevel * ratio)
             / (static_cast<SampleType> (1.0) - ratio);
    }

    void beginStage (Stage nextStage,
                     SampleType target,
                     SampleType seconds,
                     SampleType exponentialFactor,
                     SampleType completionRatio) noexcept
    {
        stage = nextStage;
        stageTarget = target;

        if (seconds <= 0.0f || std::abs (current - target) <= minimumDistance)
        {
            current = target;
            level = target;
            stageDifference = 0.0f;
            stageIsLinear = false;
            return;
        }

        if (shape == EnvelopeShape::linear)
        {
            stageIsLinear = true;
            level = target;
            stageDifference = current - target;
            stageLinearStep = std::max (minimumDistance,
                                        std::abs (stageDifference)
                                            * secondsToLinearRate (seconds));
            stageThreshold = minimumDistance;
            return;
        }

        stageIsLinear = false;
        factor = exponentialFactor;
        level = finiteTimeAsymptote (current, target, completionRatio);
        stageDifference = current - level;
        stageThreshold = stageCompletionThreshold (completionRatio);
    }

    void beginDecay() noexcept
    {
        beginStage (Stage::decay,
                    sustainLevel,
                    decaySeconds,
                    decayFactor,
                    envelopeCompletionRatio);
    }

    void beginAttack() noexcept
    {
        beginStage (Stage::attack,
                    1.0f,
                    attackSeconds,
                    attackFactor,
                    attackCompletionRatio);

        if (stageDifference == 0.0f)
            beginDecay();
    }

    void beginRelease() noexcept
    {
        if (releaseSeconds <= 0.0f)
        {
            current = 0.0f;
            stageDifference = 0.0f;
            level = 0.0f;
            stageTarget = 0.0f;
            stage = Stage::idle;
            return;
        }

        beginStage (Stage::release,
                    0.0f,
                    releaseSeconds,
                    releaseFactor,
                    releaseCompletionRatio);
    }

    void advanceActiveStage() noexcept
    {
        if (stageIsLinear)
        {
            if (stageDifference > 0.0f)
                stageDifference = std::max (SampleType {},
                                            stageDifference - stageLinearStep);
            else
                stageDifference = std::min (SampleType {},
                                            stageDifference + stageLinearStep);
        }
        else
        {
            stageDifference *= factor;
        }
    }

    void startFromLevel (SampleType startLevel) noexcept
    {
        current = std::max (SampleType {}, startLevel);
        stageDifference = 0.0f;

        if (current >= 1.0f)
            beginDecay();
        else
            beginAttack();
    }

    SampleType sampleRate = static_cast<SampleType> (48000.0);
    SampleType attackSeconds {};
    SampleType decaySeconds {};
    SampleType sustainLevel = static_cast<SampleType> (1.0);
    SampleType releaseSeconds = static_cast<SampleType> (0.001);
    SampleType current {};
    SampleType level {};
    SampleType stageTarget {};
    SampleType stageDifference {};
    SampleType stageThreshold = minimumDistance;
    SampleType factor {};
    SampleType attackFactor {};
    SampleType decayFactor {};
    SampleType releaseFactor {};
    SampleType stageLinearStep {};
    SampleType forcedReleaseStep {};
    bool stageIsLinear = false;
    EnvelopeShape shape = EnvelopeShape::exponential;
    Stage stage = Stage::idle;
};

} // namespace chardsp
