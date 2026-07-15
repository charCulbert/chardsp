#pragma once

#include <algorithm>
#include <cmath>

namespace chardsp
{

enum class EnvelopeShape
{
    exponential,
    linear
};

// Finite-time ADSR with stages that may be edited while they are running.
struct ADSREnvelope
{
    void prepare (float newSampleRate) noexcept
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

    void setAttack (float seconds) noexcept
    {
        attackSeconds = sanitiseSeconds (seconds);
        attackFactor = secondsToExponentialFactor (attackSeconds, attackCompletionRatio);

        if (stage == Stage::attack)
            beginAttack();
    }

    void setDecay (float seconds) noexcept
    {
        decaySeconds = sanitiseSeconds (seconds);
        decayFactor = secondsToExponentialFactor (decaySeconds, envelopeCompletionRatio);

        if (stage == Stage::decay)
            beginDecay();
    }

    void setSustain (float nextLevel) noexcept
    {
        sustainLevel = std::isfinite (nextLevel)
            ? std::clamp (nextLevel, 0.0f, 1.0f)
            : 0.0f;

        if (stage == Stage::decay)
            beginDecay();
    }

    void setRelease (float seconds) noexcept
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
    void forceRelease (float seconds) noexcept
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

    float next() noexcept
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
                stageDifference = std::max (0.0f, stageDifference - forcedReleaseStep);
            else
                stageDifference = std::min (0.0f, stageDifference + forcedReleaseStep);

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

    float getCurrent() const noexcept { return current; }
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

    static constexpr float envelopeCompletionRatio = 0.047f;
    static constexpr float attackCompletionRatio = 0.040f;
    static constexpr float releaseCompletionRatio = 0.049f;
    static constexpr float minimumDistance = 1.0e-7f;

    static float sanitiseSeconds (float seconds) noexcept
    {
        return std::isfinite (seconds) ? std::max (0.0f, seconds) : 0.0f;
    }

    float secondsToLinearRate (float seconds) const noexcept
    {
        return 1.0f / std::max (1.0f, seconds * sampleRate);
    }

    float secondsToExponentialFactor (float seconds, float completionRatio) const noexcept
    {
        const auto samples = std::max (1.0f, seconds * sampleRate);
        return std::clamp (std::pow (completionRatio, 1.0f / samples),
                           0.0f,
                           0.999999f);
    }

    float stageCompletionThreshold (float completionRatio) const noexcept
    {
        return std::max (minimumDistance, std::abs (stageDifference) * completionRatio);
    }

    static float finiteTimeAsymptote (float startLevel,
                                      float targetLevel,
                                      float completionRatio) noexcept
    {
        const auto ratio = std::clamp (completionRatio, 0.000001f, 0.95f);
        return (targetLevel - startLevel * ratio) / (1.0f - ratio);
    }

    void beginStage (Stage nextStage,
                     float target,
                     float seconds,
                     float exponentialFactor,
                     float completionRatio) noexcept
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
                stageDifference = std::max (0.0f, stageDifference - stageLinearStep);
            else
                stageDifference = std::min (0.0f, stageDifference + stageLinearStep);
        }
        else
        {
            stageDifference *= factor;
        }
    }

    void startFromLevel (float startLevel) noexcept
    {
        current = std::max (0.0f, startLevel);
        stageDifference = 0.0f;

        if (current >= 1.0f)
            beginDecay();
        else
            beginAttack();
    }

    float sampleRate = 48000.0f;
    float attackSeconds = 0.0f;
    float decaySeconds = 0.0f;
    float sustainLevel = 1.0f;
    float releaseSeconds = 0.001f;
    float current = 0.0f;
    float level = 0.0f;
    float stageTarget = 0.0f;
    float stageDifference = 0.0f;
    float stageThreshold = minimumDistance;
    float factor = 0.0f;
    float attackFactor = 0.0f;
    float decayFactor = 0.0f;
    float releaseFactor = 0.0f;
    float stageLinearStep = 0.0f;
    float forcedReleaseStep = 0.0f;
    bool stageIsLinear = false;
    EnvelopeShape shape = EnvelopeShape::exponential;
    Stage stage = Stage::idle;
};

} // namespace chardsp
