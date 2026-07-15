#pragma once

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
//
// This combines Nomad's poly note gate and release-aware allocator. It keeps
// physical key state separate from voice-slot state, which is important after
// a held voice has been stolen.
template <typename Voice, size_t voiceCount>
struct VoicePool
{
    static_assert (voiceCount > 0, "A voice pool needs at least one voice");

    static constexpr float sameNoteFadeSeconds = 0.002f;
    static constexpr float releasedNoteMemorySeconds = 10.0f;
    static constexpr size_t heldNoteCapacity = 32;

    void prepare (float newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        voiceStates = {};
        heldNotes = {};
        pending = {};
        sustainActive = false;
        nextAge = 1;

        for (size_t i = 0; i < voiceCount; ++i)
            voices[i].prepare (sampleRate, i);
    }

    void noteOn (int note, float velocity, uint8_t channel = 0) noexcept
    {
        if (const auto existing = findHeldNote (note, channel); existing >= 0)
        {
            const auto index = static_cast<size_t> (existing);

            if (heldNotes[index].keyHeld)
                return;

            forceReleaseVoiceForNote (note, channel);
            clearHeldNote (index);
            scheduleSameNoteRetrigger (note, velocity, channel);
            return;
        }

        rememberHeldNote (note, channel);
        startAllocatedVoice (note, velocity, channel);
    }

    void noteOff (int note, uint8_t channel = 0) noexcept
    {
        if (pending.active && pending.note == note
            && pending.channel == channel)
        {
            pending.active = false;
            return;
        }

        const auto held = findHeldNote (note, channel);

        if (held < 0)
            return;

        auto& heldState = heldNotes[static_cast<size_t> (held)];
        heldState.keyHeld = false;

        if (sustainActive)
        {
            heldState.releaseSamplesRemaining = 0.0f;
            return;
        }

        releaseAllocatedVoice (note, channel);
        heldState.releaseSamplesRemaining = releaseMemorySamples();
    }

    void setSustain (bool active) noexcept
    {
        if (sustainActive == active)
            return;

        sustainActive = active;

        if (! sustainActive)
            releaseSustainedNotes();
    }

    void allNotesOff() noexcept
    {
        pending.active = false;
        sustainActive = false;

        for (size_t i = 0; i < voiceCount; ++i)
        {
            auto& state = voiceStates[i];

            if (! state.allocated || ! state.gate)
                continue;

            state.gate = false;
            state.releasing = true;
            state.releaseSamplesRemaining = releaseMemorySamples();
            voices[i].release();
        }

        heldNotes = {};
    }

    void forceReleaseAll (float seconds = sameNoteFadeSeconds) noexcept
    {
        pending.active = false;
        sustainActive = false;
        heldNotes = {};

        for (size_t i = 0; i < voiceCount; ++i)
        {
            auto& state = voiceStates[i];

            if (! state.allocated && ! voices[i].isActive())
                continue;

            voices[i].forceRelease (seconds);

            if (state.allocated)
            {
                state.gate = false;
                state.releasing = true;
                state.releaseSamplesRemaining = releaseMemorySamples();
            }
        }
    }

    float next() noexcept
    {
        float mix = 0.0f;

        for (size_t i = 0; i < voiceCount; ++i)
            if (voiceStates[i].allocated || voices[i].isActive())
                mix += voices[i].next();

        ageReleasedVoices();
        ageReleasedHeldNotes();
        advancePendingRetrigger();
        return mix;
    }

    Voice& getVoice (size_t index) noexcept { return voices[index]; }
    const Voice& getVoice (size_t index) const noexcept { return voices[index]; }

    size_t getActiveVoiceCount() const noexcept
    {
        size_t active = 0;

        for (const auto& voice : voices)
            if (voice.isActive())
                ++active;

        return active;
    }

    size_t getAllocatedVoiceCount() const noexcept
    {
        size_t allocated = 0;

        for (const auto& state : voiceStates)
            if (state.allocated)
                ++allocated;

        return allocated;
    }

    int findVoiceForNote (int note, uint8_t channel = 0) const noexcept
    {
        return findAllocatedVoice (note, channel);
    }

    bool isNoteHeld (int note, uint8_t channel = 0) const noexcept
    {
        const auto held = findHeldNote (note, channel);
        return held >= 0
            && heldNotes[static_cast<size_t> (held)].keyHeld;
    }

private:
    struct VoiceState
    {
        int note = -1;
        uint8_t channel = 0;
        bool allocated = false;
        bool gate = false;
        bool releasing = false;
        uint64_t age = 0;
        float releaseSamplesRemaining = 0.0f;
    };

    struct HeldNote
    {
        int note = -1;
        uint8_t channel = 0;
        bool active = false;
        bool keyHeld = false;
        float releaseSamplesRemaining = 0.0f;
    };

    struct PendingRetrigger
    {
        bool active = false;
        int note = -1;
        float velocity = 0.0f;
        uint8_t channel = 0;
        float samplesRemaining = 0.0f;
    };

    float releaseMemorySamples() const noexcept
    {
        const auto samples = releasedNoteMemorySeconds * sampleRate;
        return samples > 1.0f ? samples : 1.0f;
    }

    float retriggerFadeSamples() const noexcept
    {
        const auto samples = sameNoteFadeSeconds * sampleRate;
        return samples > 1.0f ? samples : 1.0f;
    }

    void startAllocatedVoice (int note, float velocity,
                              uint8_t channel) noexcept
    {
        const auto index = chooseVoice();
        auto& state = voiceStates[index];

        if (state.allocated && state.gate)
            voices[index].release();

        state.note = note;
        state.channel = channel;
        state.allocated = true;
        state.gate = true;
        state.releasing = false;
        state.age = nextAge++;
        state.releaseSamplesRemaining = 0.0f;
        voices[index].start (note, velocity);
    }

    size_t chooseVoice() const noexcept
    {
        for (size_t i = 0; i < voiceCount; ++i)
            if (! voiceStates[i].allocated)
                return i;

        size_t selected = voiceCount;
        auto oldestAge = std::numeric_limits<uint64_t>::max();

        for (size_t i = 0; i < voiceCount; ++i)
        {
            const auto& state = voiceStates[i];

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
            if (voiceStates[i].age < oldestAge)
            {
                selected = i;
                oldestAge = voiceStates[i].age;
            }
        }

        return selected;
    }

    void releaseAllocatedVoice (int note, uint8_t channel) noexcept
    {
        const auto held = findHeldAllocatedVoice (note, channel);

        if (held < 0)
            return;

        const auto index = static_cast<size_t> (held);
        auto& state = voiceStates[index];
        voices[index].release();
        state.gate = false;
        state.releasing = true;
        state.releaseSamplesRemaining = releaseMemorySamples();
    }

    void forceReleaseVoiceForNote (int note, uint8_t channel) noexcept
    {
        const auto allocated = findAllocatedVoice (note, channel);

        if (allocated < 0)
            return;

        const auto index = static_cast<size_t> (allocated);
        auto& state = voiceStates[index];
        voices[index].forceRelease (sameNoteFadeSeconds);
        state.gate = false;
        state.releasing = true;
        state.releaseSamplesRemaining = releaseMemorySamples();
    }

    void rememberHeldNote (int note, uint8_t channel) noexcept
    {
        auto slot = findFreeHeldNote();

        if (slot < 0)
        {
            slot = 0;
            const auto& old = heldNotes[0];
            releaseAllocatedVoice (old.note, old.channel);
        }

        auto& held = heldNotes[static_cast<size_t> (slot)];
        held.note = note;
        held.channel = channel;
        held.active = true;
        held.keyHeld = true;
        held.releaseSamplesRemaining = 0.0f;
    }

    void scheduleSameNoteRetrigger (int note, float velocity,
                                    uint8_t channel) noexcept
    {
        pending.active = true;
        pending.note = note;
        pending.velocity = velocity;
        pending.channel = channel;
        pending.samplesRemaining = retriggerFadeSamples();
    }

    void advancePendingRetrigger() noexcept
    {
        if (! pending.active || (pending.samplesRemaining -= 1.0f) > 0.0f)
            return;

        const auto note = pending.note;
        const auto velocity = pending.velocity;
        const auto channel = pending.channel;
        pending.active = false;
        rememberHeldNote (note, channel);

        if (const auto same = findAllocatedVoice (note, channel); same >= 0)
        {
            const auto index = static_cast<size_t> (same);
            auto& state = voiceStates[index];
            state.gate = true;
            state.releasing = false;
            state.age = nextAge++;
            state.releaseSamplesRemaining = 0.0f;
            voices[index].start (note, velocity);
            return;
        }

        startAllocatedVoice (note, velocity, channel);
    }

    void releaseSustainedNotes() noexcept
    {
        for (auto& held : heldNotes)
        {
            if (! held.active || held.keyHeld
                || held.releaseSamplesRemaining > 0.0f)
                continue;

            releaseAllocatedVoice (held.note, held.channel);
            held.releaseSamplesRemaining = releaseMemorySamples();
        }
    }

    void ageReleasedVoices() noexcept
    {
        for (auto& state : voiceStates)
        {
            if (! state.allocated || ! state.releasing)
                continue;

            state.releaseSamplesRemaining -= 1.0f;

            if (state.releaseSamplesRemaining <= 0.0f)
                state = {};
        }
    }

    void ageReleasedHeldNotes() noexcept
    {
        for (auto& held : heldNotes)
        {
            if (! held.active || held.keyHeld
                || held.releaseSamplesRemaining <= 0.0f)
                continue;

            held.releaseSamplesRemaining -= 1.0f;

            if (held.releaseSamplesRemaining <= 0.0f)
                held = {};
        }
    }

    void clearHeldNote (size_t index) noexcept
    {
        heldNotes[index] = {};
    }

    int findAllocatedVoice (int note, uint8_t channel) const noexcept
    {
        for (size_t i = 0; i < voiceCount; ++i)
        {
            const auto& state = voiceStates[i];

            if (state.allocated && state.note == note
                && state.channel == channel)
                return static_cast<int> (i);
        }

        return -1;
    }

    int findHeldAllocatedVoice (int note, uint8_t channel) const noexcept
    {
        for (size_t i = 0; i < voiceCount; ++i)
        {
            const auto& state = voiceStates[i];

            if (state.allocated && state.gate && state.note == note
                && state.channel == channel)
                return static_cast<int> (i);
        }

        return -1;
    }

    int findHeldNote (int note, uint8_t channel) const noexcept
    {
        for (size_t i = 0; i < heldNoteCapacity; ++i)
        {
            const auto& held = heldNotes[i];

            if (held.active && held.note == note && held.channel == channel)
                return static_cast<int> (i);
        }

        return -1;
    }

    int findFreeHeldNote() const noexcept
    {
        for (size_t i = 0; i < heldNoteCapacity; ++i)
            if (! heldNotes[i].active)
                return static_cast<int> (i);

        for (size_t i = 0; i < heldNoteCapacity; ++i)
            if (! heldNotes[i].keyHeld)
                return static_cast<int> (i);

        return -1;
    }

    std::array<Voice, voiceCount> voices;
    std::array<VoiceState, voiceCount> voiceStates {};
    std::array<HeldNote, heldNoteCapacity> heldNotes {};
    PendingRetrigger pending;
    float sampleRate = 48000.0f;
    uint64_t nextAge = 1;
    bool sustainActive = false;
};

} // namespace chardsp
