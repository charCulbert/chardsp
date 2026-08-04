#pragma once

#include "chardsp/chardsp_OpenSourceLicenses.h"

#include <array>
#include <cmath>
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
//   prepare (SampleType sampleRate, size_t voiceIndex)
//   start (const Key& key, SampleType velocity)
//   release(), forceRelease (SampleType seconds), next(), and isAvailable(),
// where SampleType is the float or double returned by Voice::next().
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

    using SampleType = std::remove_cv_t<std::remove_reference_t<
        decltype (std::declval<Voice&>().next())>>;

    static_assert (std::is_same_v<SampleType, float>
                   || std::is_same_v<SampleType, double>,
                   "Voice::next() must return float or double");

    static constexpr SampleType defaultStealFadeSeconds =
        static_cast<SampleType> (0.002);

    void prepare (SampleType newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        voiceStates = {};
        pendingStarts = {};
        sustainActive = false;

        for (size_t i = 0; i < voiceCount; ++i)
            voices[i].prepare (sampleRate, i);
    }

    // A non-zero time lets the selected old voice reach silence before its
    // replacement starts. Zero selects immediate replacement.
    void setStealFadeTime (SampleType seconds) noexcept
    {
        stealFadeSeconds = std::isfinite (seconds) && seconds > SampleType {}
            ? seconds : SampleType {};
    }

    SampleType getStealFadeTime() const noexcept { return stealFadeSeconds; }

    // Starts a caller-defined key. Repeated starts while that key still owns a
    // held voice are ignored. If no slot is free, the selected old voice begins
    // its forced release before the pending key starts in that same slot.
    void start (const Key& key, SampleType velocity) noexcept
    {
        if (findPendingVoice (key) >= 0)
            return;

        if (const auto existing = findAllocatedVoice (key); existing >= 0)
        {
            const auto index = static_cast<size_t> (existing);

            if (voiceStates[index].keyHeld)
                return;

            replaceVoice (index, key, velocity);
            return;
        }

        startAllocatedVoice (key, velocity);
    }

    // Stops a key only if it still owns a held voice. Stops for keys whose
    // voices were already stolen are harmless.
    void stop (const Key& key) noexcept
    {
        if (const auto pending = findPendingVoice (key); pending >= 0)
        {
            pendingStarts[static_cast<size_t> (pending)] = {};
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

    // Releases every active voice normally. Pending starts and sustain are
    // cleared so no voice remains gated after the release begins.
    void releaseAll() noexcept
    {
        pendingStarts = {};
        sustainActive = false;
        nextAge = 1;

        for (size_t i = 0; i < voiceCount; ++i)
        {
            auto& state = voiceStates[i];

            if (! state.allocated || ! state.gate)
                continue;

            state.keyHeld = false;
            releaseVoice (i);
        }
    }

    // Immediately silences every voice and clears all allocation state.
    void reset() noexcept
    {
        pendingStarts = {};
        sustainActive = false;
        nextAge = 1;

        for (size_t i = 0; i < voiceCount; ++i)
        {
            auto& state = voiceStates[i];

            if (! state.allocated && voices[i].isAvailable())
                continue;

            voices[i].forceRelease (SampleType {});
            state = {};
        }
    }

    SampleType next() noexcept
    {
        SampleType mix {};

        for (size_t i = 0; i < voiceCount; ++i)
        {
            // Like a conventional synth render callback, every fixed voice is
            // ticked. An available Voice owns its cheap path: it can simply
            // return zero or advance a free-running source first.
            mix += voices[i].next();

            if (pendingStarts[i].active && voices[i].isAvailable())
                startPendingVoice (i);
        }

        reclaimAvailableVoices();
        return mix;
    }

    Voice& getVoice (size_t index) noexcept { return voices[index]; }
    const Voice& getVoice (size_t index) const noexcept { return voices[index]; }

    size_t getActiveVoiceCount() const noexcept
    {
        size_t active = 0;

        for (const auto& voice : voices)
            if (! voice.isAvailable())
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
        if (const auto pending = findPendingVoice (key); pending >= 0)
            return pending;

        return findAllocatedVoice (key);
    }

    bool isKeyHeld (const Key& key) const noexcept
    {
        if (findPendingVoice (key) >= 0)
            return true;

        const auto voice = findHeldVoice (key);
        return voice >= 0;
    }

    bool isPending (const Key& key) const noexcept
    {
        return findPendingVoice (key) >= 0;
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

    struct PendingStart
    {
        Key key {};
        SampleType velocity {};
        uint64_t age = 0;
        bool active = false;
    };

    void startAllocatedVoice (const Key& key, SampleType velocity) noexcept
    {
        const auto index = chooseVoice();

        if (pendingStarts[index].active)
        {
            pendingStarts[index] = { key, velocity, nextAge++, true };
            return;
        }

        if (voiceStates[index].allocated || ! voices[index].isAvailable())
        {
            replaceVoice (index, key, velocity);
            return;
        }

        startVoice (index, key, velocity, nextAge++);
    }

    void startVoice (size_t index, const Key& key, SampleType velocity,
                     uint64_t age) noexcept
    {
        auto& state = voiceStates[index];

        state.key = key;
        state.allocated = true;
        state.keyHeld = true;
        state.gate = true;
        state.releasing = false;
        state.age = age;
        voices[index].start (key, velocity);
    }

    void replaceVoice (size_t index, const Key& key,
                       SampleType velocity) noexcept
    {
        const auto age = nextAge++;

        if (stealFadeSeconds <= SampleType {} || voices[index].isAvailable())
        {
            if (! voices[index].isAvailable())
                voices[index].forceRelease (SampleType {});

            pendingStarts[index] = {};
            startVoice (index, key, velocity, age);
            return;
        }

        voices[index].forceRelease (stealFadeSeconds);

        if (voices[index].isAvailable())
        {
            startVoice (index, key, velocity, age);
            return;
        }

        auto& state = voiceStates[index];
        state.keyHeld = false;
        state.gate = false;
        state.releasing = true;
        pendingStarts[index] = { key, velocity, age, true };
    }

    size_t chooseVoice() const noexcept
    {
        for (size_t i = 0; i < voiceCount; ++i)
            if (! voiceStates[i].allocated && ! pendingStarts[i].active
                && voices[i].isAvailable())
                return i;

        size_t selected = voiceCount;
        auto oldestAge = std::numeric_limits<uint64_t>::max();

        for (size_t i = 0; i < voiceCount; ++i)
        {
            const auto& state = voiceStates[i];

            if (! pendingStarts[i].active && state.releasing
                && state.age < oldestAge)
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
            if (! pendingStarts[i].active
                && voiceStates[i].age < oldestAge)
            {
                selected = i;
                oldestAge = voiceStates[i].age;
            }
        }

        if (selected < voiceCount)
            return selected;

        oldestAge = std::numeric_limits<uint64_t>::max();

        for (size_t i = 0; i < voiceCount; ++i)
        {
            if (pendingStarts[i].active
                && pendingStarts[i].age < oldestAge)
            {
                selected = i;
                oldestAge = pendingStarts[i].age;
            }
        }

        return selected < voiceCount ? selected : size_t {};
    }

    void releaseVoice (size_t index) noexcept
    {
        auto& state = voiceStates[index];
        voices[index].release();
        state.gate = false;
        state.releasing = true;

        if (voices[index].isAvailable())
            state = {};
    }

    void startPendingVoice (size_t index) noexcept
    {
        const auto pending = pendingStarts[index];
        pendingStarts[index] = {};
        startVoice (index, pending.key, pending.velocity, pending.age);
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

    void reclaimAvailableVoices() noexcept
    {
        for (size_t i = 0; i < voiceCount; ++i)
            if (! pendingStarts[i].active && voiceStates[i].allocated
                && voices[i].isAvailable())
                voiceStates[i] = {};
    }

    int findAllocatedVoice (const Key& key) const noexcept
    {
        for (size_t i = 0; i < voiceCount; ++i)
        {
            const auto& state = voiceStates[i];

            if (! pendingStarts[i].active && state.allocated
                && state.key == key)
                return static_cast<int> (i);
        }

        return -1;
    }

    int findPendingVoice (const Key& key) const noexcept
    {
        for (size_t i = 0; i < voiceCount; ++i)
            if (pendingStarts[i].active && pendingStarts[i].key == key)
                return static_cast<int> (i);

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
    std::array<PendingStart, voiceCount> pendingStarts {};
    SampleType sampleRate = static_cast<SampleType> (48000.0);
    SampleType stealFadeSeconds = defaultStealFadeSeconds;
    uint64_t nextAge = 1;
    bool sustainActive = false;
};

} // namespace chardsp
