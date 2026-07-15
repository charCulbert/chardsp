#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace chardsp
{

// Key is a small, caller-defined identity type. It can represent a MIDI
// note/channel pair, an analog key, a sequencer event, or anything else with
// equality comparison. Voice must provide:
//   prepare (float sampleRate, size_t voiceIndex)
//   start (const Key& key, float velocity)
//   release(), forceRelease (float seconds), next(), and isActive()
//
// The pool deliberately stores only its fixed set of voice slots. Input
// scanning, debounce, and physical-key history after a voice is stolen belong
// to the caller. This keeps the allocator below MIDI and makes its memory use
// independent of the number of possible input keys.
template <typename Voice, size_t voiceCount, typename Key = int>
struct VoicePool
{
    static_assert (voiceCount > 0, "A voice pool needs at least one voice");
    static_assert (std::is_nothrow_default_constructible_v<Key>,
                   "VoicePool Key must be nothrow default constructible");
    static_assert (std::is_nothrow_copy_constructible_v<Key>,
                   "VoicePool Key must be nothrow copy constructible");
    static_assert (std::is_nothrow_copy_assignable_v<Key>,
                   "VoicePool Key must be nothrow copy assignable");
    static_assert (noexcept (std::declval<const Key&>()
                             == std::declval<const Key&>()),
                   "VoicePool Key equality must be noexcept");

    static constexpr float sameKeyFadeSeconds = 0.002f;

    void prepare (float newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        voiceStates = {};
        pending = {};
        sustainActive = false;
        nextAge = 1;

        for (size_t i = 0; i < voiceCount; ++i)
            voices[i].prepare (sampleRate, i);
    }

    // Starts a caller-defined key. Repeated starts while that key still owns a
    // held voice are ignored. Re-starting its retained release tail performs
    // Nomad's 2 ms fade before using the same slot.
    void start (const Key& key, float velocity) noexcept
    {
        if (pending.active && pending.key == key)
            return;

        if (const auto existing = findAllocatedVoice (key); existing >= 0)
        {
            const auto index = static_cast<size_t> (existing);

            if (voiceStates[index].keyHeld)
                return;

            forceReleaseVoice (index);

            if (voiceStates[index].allocated)
                scheduleSameKeyRetrigger (key, velocity);
            else
                startAllocatedVoice (key, velocity);

            return;
        }

        startAllocatedVoice (key, velocity);
    }

    // Stops a key only if it still owns a held voice. Stops for keys whose
    // voices were already stolen are harmless.
    void stop (const Key& key) noexcept
    {
        if (pending.active && pending.key == key)
        {
            pending.active = false;
            return;
        }

        const auto held = findHeldVoice (key);

        if (held < 0)
            return;

        const auto index = static_cast<size_t> (held);
        voiceStates[index].keyHeld = false;

        if (! sustainActive)
            releaseVoice (index);
    }

    void setSustain (bool active) noexcept
    {
        if (sustainActive == active)
            return;

        sustainActive = active;

        if (! sustainActive)
            releaseSustainedVoices();
    }

    void allKeysOff() noexcept
    {
        pending.active = false;
        sustainActive = false;

        for (size_t i = 0; i < voiceCount; ++i)
        {
            auto& state = voiceStates[i];

            if (! state.allocated || ! state.gate)
                continue;

            state.keyHeld = false;
            releaseVoice (i);
        }
    }

    void forceReleaseAll (float seconds = sameKeyFadeSeconds) noexcept
    {
        pending.active = false;
        sustainActive = false;

        for (size_t i = 0; i < voiceCount; ++i)
        {
            auto& state = voiceStates[i];

            if (! state.allocated && ! voices[i].isActive())
                continue;

            voices[i].forceRelease (seconds);

            if (state.allocated)
            {
                state.keyHeld = false;
                state.gate = false;
                state.releasing = true;

                if (! voices[i].isActive())
                    state = {};
            }
        }
    }

    float next() noexcept
    {
        // Cmajor's note gate advances its delayed retrigger before the voices
        // render this frame, so a completed 2 ms fade restarts sample-exactly.
        advancePendingRetrigger();
        float mix = 0.0f;

        for (size_t i = 0; i < voiceCount; ++i)
            if (voiceStates[i].allocated || voices[i].isActive())
                mix += voices[i].next();

        reclaimSilentVoices();
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

    int findVoice (const Key& key) const noexcept
    {
        return findAllocatedVoice (key);
    }

    bool isKeyHeld (const Key& key) const noexcept
    {
        const auto voice = findHeldVoice (key);
        return voice >= 0;
    }

private:
    struct VoiceState
    {
        Key key {};
        bool allocated = false;
        bool keyHeld = false;
        bool gate = false;
        bool releasing = false;
        uint64_t age = 0;
    };

    struct PendingRetrigger
    {
        Key key {};
        float velocity = 0.0f;
        float samplesRemaining = 0.0f;
        bool active = false;
    };

    float retriggerFadeSamples() const noexcept
    {
        const auto samples = sameKeyFadeSeconds * sampleRate;
        return samples > 1.0f ? samples : 1.0f;
    }

    void startAllocatedVoice (const Key& key, float velocity) noexcept
    {
        const auto index = chooseVoice();
        auto& state = voiceStates[index];

        if (state.allocated && state.gate)
            voices[index].release();

        state.key = key;
        state.allocated = true;
        state.keyHeld = true;
        state.gate = true;
        state.releasing = false;
        state.age = nextAge++;
        voices[index].start (key, velocity);
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

    void releaseVoice (size_t index) noexcept
    {
        auto& state = voiceStates[index];
        voices[index].release();
        state.gate = false;
        state.releasing = true;

        if (! voices[index].isActive())
            state = {};
    }

    void forceReleaseVoice (size_t index) noexcept
    {
        auto& state = voiceStates[index];
        voices[index].forceRelease (sameKeyFadeSeconds);
        state.keyHeld = false;
        state.gate = false;
        state.releasing = true;

        if (! voices[index].isActive())
            state = {};
    }

    void scheduleSameKeyRetrigger (const Key& key, float velocity) noexcept
    {
        pending.key = key;
        pending.velocity = velocity;
        pending.samplesRemaining = retriggerFadeSamples();
        pending.active = true;
    }

    void advancePendingRetrigger() noexcept
    {
        if (! pending.active || (pending.samplesRemaining -= 1.0f) > 0.0f)
            return;

        const auto key = pending.key;
        const auto velocity = pending.velocity;
        pending.active = false;

        if (const auto same = findAllocatedVoice (key); same >= 0)
        {
            const auto index = static_cast<size_t> (same);
            auto& state = voiceStates[index];
            state.keyHeld = true;
            state.gate = true;
            state.releasing = false;
            state.age = nextAge++;
            voices[index].start (key, velocity);
            return;
        }

        startAllocatedVoice (key, velocity);
    }

    void releaseSustainedVoices() noexcept
    {
        for (size_t i = 0; i < voiceCount; ++i)
        {
            const auto& state = voiceStates[i];

            if (state.allocated && state.gate && ! state.keyHeld)
                releaseVoice (i);
        }
    }

    void reclaimSilentVoices() noexcept
    {
        for (size_t i = 0; i < voiceCount; ++i)
            if (voiceStates[i].allocated && ! voices[i].isActive())
                voiceStates[i] = {};
    }

    int findAllocatedVoice (const Key& key) const noexcept
    {
        for (size_t i = 0; i < voiceCount; ++i)
        {
            const auto& state = voiceStates[i];

            if (state.allocated && state.key == key)
                return static_cast<int> (i);
        }

        return -1;
    }

    int findHeldVoice (const Key& key) const noexcept
    {
        for (size_t i = 0; i < voiceCount; ++i)
        {
            const auto& state = voiceStates[i];

            if (state.allocated && state.gate && state.keyHeld
                && state.key == key)
                return static_cast<int> (i);
        }

        return -1;
    }

    std::array<Voice, voiceCount> voices;
    std::array<VoiceState, voiceCount> voiceStates {};
    PendingRetrigger pending;
    float sampleRate = 48000.0f;
    uint64_t nextAge = 1;
    bool sustainActive = false;
};

} // namespace chardsp
