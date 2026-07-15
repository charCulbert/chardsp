#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace chardsp
{

// Voice must provide:
//   prepare (float sampleRate, size_t voiceIndex)
//   start (int note, float velocity)
//   release(), forceRelease (float seconds), next(), and isActive()
template <typename Voice, size_t voiceCount>
struct VoicePool
{
    static_assert (voiceCount > 0, "A voice pool needs at least one voice");

    void prepare (float newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        states = {};
        pending = {};
        nextAge = 1;

        for (size_t i = 0; i < voiceCount; ++i)
            voices[i].prepare (sampleRate, i);
    }

    void noteOn (int note, float velocity) noexcept
    {
        if (findHeldVoice (note) >= 0)
            return;

        if (const auto sameNote = findAllocatedVoice (note); sameNote >= 0)
        {
            constexpr float sameNoteFadeSeconds = 0.002f;
            const auto index = static_cast<size_t> (sameNote);
            markReleased (index);
            voices[index].forceRelease (sameNoteFadeSeconds);
            pending.active = true;
            pending.note = note;
            pending.velocity = velocity;
            pending.voiceIndex = index;
            pending.samplesRemaining = std::max (1,
                static_cast<int> (sameNoteFadeSeconds * sampleRate + 0.5f));
            return;
        }

        startVoice (note, velocity);
    }

    void noteOff (int note) noexcept
    {
        if (pending.active && pending.note == note)
        {
            pending.active = false;
            return;
        }

        if (const auto held = findHeldVoice (note); held >= 0)
        {
            const auto index = static_cast<size_t> (held);
            markReleased (index);
            voices[index].release();
        }
    }

    void allNotesOff() noexcept
    {
        pending.active = false;

        for (size_t i = 0; i < voiceCount; ++i)
        {
            if (! states[i].active || ! states[i].gate)
                continue;

            markReleased (i);
            voices[i].release();
        }
    }

    void forceReleaseAll (float seconds = 0.002f) noexcept
    {
        pending.active = false;

        for (size_t i = 0; i < voiceCount; ++i)
        {
            if (! states[i].active)
                continue;

            markReleased (i);
            voices[i].forceRelease (seconds);
        }
    }

    float next() noexcept
    {
        float mix = 0.0f;

        for (size_t i = 0; i < voiceCount; ++i)
        {
            if (! states[i].active)
                continue;

            mix += voices[i].next();

            const auto reservedForRetrigger = pending.active
                                           && pending.voiceIndex == i;
            if (! voices[i].isActive() && states[i].releasing
                && ! reservedForRetrigger)
                deactivate (i);
        }

        advancePendingRetrigger();
        return mix;
    }

    Voice& getVoice (size_t index) noexcept { return voices[index]; }
    const Voice& getVoice (size_t index) const noexcept { return voices[index]; }

    size_t getActiveVoiceCount() const noexcept
    {
        size_t active = 0;

        for (const auto& state : states)
            if (state.active)
                ++active;

        return active;
    }

    int findVoiceForNote (int note) const noexcept
    {
        return findAllocatedVoice (note);
    }

private:
    struct VoiceState
    {
        int note = -1;
        bool active = false;
        bool gate = false;
        bool releasing = false;
        uint64_t age = 0;
    };

    struct PendingRetrigger
    {
        bool active = false;
        int note = -1;
        float velocity = 0.0f;
        size_t voiceIndex = voiceCount;
        int samplesRemaining = 0;
    };

    void startVoice (int note, float velocity) noexcept
    {
        const auto index = chooseVoice (note);

        if (pending.active && pending.voiceIndex == index)
            pending.active = false;

        auto& state = states[index];
        state.note = note;
        state.active = true;
        state.gate = true;
        state.releasing = false;
        state.age = nextAge++;
        voices[index].start (note, velocity);
    }

    size_t chooseVoice (int note) const noexcept
    {
        if (const auto sameNote = findAllocatedVoice (note); sameNote >= 0)
            return static_cast<size_t> (sameNote);

        for (size_t i = 0; i < voiceCount; ++i)
            if (! states[i].active)
                return i;

        size_t selected = voiceCount;
        auto oldestAge = std::numeric_limits<uint64_t>::max();

        for (size_t i = 0; i < voiceCount; ++i)
        {
            const auto& state = states[i];

            if (state.releasing && state.age < oldestAge)
            {
                selected = i;
                oldestAge = state.age;
            }
        }

        if (selected < voiceCount)
            return selected;

        oldestAge = std::numeric_limits<uint64_t>::max();

        for (size_t i = 0; i < voiceCount; ++i)
        {
            if (states[i].age < oldestAge)
            {
                selected = i;
                oldestAge = states[i].age;
            }
        }

        return selected;
    }

    void markReleased (size_t index) noexcept
    {
        states[index].gate = false;
        states[index].releasing = true;
    }

    void deactivate (size_t index) noexcept
    {
        states[index] = {};
    }

    int findAllocatedVoice (int note) const noexcept
    {
        for (size_t i = 0; i < voiceCount; ++i)
            if (states[i].active && states[i].note == note)
                return static_cast<int> (i);

        return -1;
    }

    int findHeldVoice (int note) const noexcept
    {
        for (size_t i = 0; i < voiceCount; ++i)
            if (states[i].active && states[i].gate && states[i].note == note)
                return static_cast<int> (i);

        return -1;
    }

    void advancePendingRetrigger() noexcept
    {
        if (! pending.active || --pending.samplesRemaining > 0)
            return;

        const auto note = pending.note;
        const auto velocity = pending.velocity;
        pending.active = false;
        startVoice (note, velocity);
    }

    std::array<Voice, voiceCount> voices;
    std::array<VoiceState, voiceCount> states {};
    PendingRetrigger pending;
    float sampleRate = 48000.0f;
    uint64_t nextAge = 1;
};

} // namespace chardsp
