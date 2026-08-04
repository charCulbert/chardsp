#pragma once

#include "chardsp/chardsp_OpenSourceLicenses.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace chardsp
{

enum class PortamentoMode
{
    off,
    always,
    fingered
};

// Protocol-neutral monophonic note policy. The caller applies returned
// actions to its own voice and pitch source; this type owns no DSP voice.
template <typename SampleType, typename Key, size_t heldCapacity>
struct MonoVoiceController
{
    static_assert (std::is_same_v<SampleType, float>
                   || std::is_same_v<SampleType, double>,
                   "MonoVoiceController supports float or double");
    static_assert (heldCapacity > 0,
                   "MonoVoiceController needs at least one held-key slot");
    static_assert (std::is_nothrow_default_constructible_v<Key>
                   && std::is_nothrow_copy_constructible_v<Key>
                   && std::is_nothrow_copy_assignable_v<Key>,
                   "MonoVoiceController Key must be nothrow copyable");
    static_assert (noexcept (std::declval<const Key&>()
                             == std::declval<const Key&>()),
                   "MonoVoiceController Key equality must be noexcept");

    struct Action
    {
        enum class Type
        {
            none,
            pitch,
            release
        };

        Type type = Type::none;
        Key key {};
        SampleType pitch {};
        SampleType velocity {};
        bool trigger = false;
        bool glide = false;

        bool hasPitch() const noexcept { return type == Type::pitch; }
        bool shouldRelease() const noexcept { return type == Type::release; }
    };

    static constexpr SampleType defaultLegatoGraceSeconds =
        static_cast<SampleType> (0.024);

    void prepare (SampleType newSampleRate) noexcept
    {
        sampleRate = std::isfinite (newSampleRate)
                     && newSampleRate > SampleType {}
            ? newSampleRate : static_cast<SampleType> (48000);
        reset();
    }

    void reset() noexcept
    {
        held = {};
        sustainActive = false;
        gateActive = false;
        pendingGateOff = false;
        hasPreviousPitch = false;
        nextOrder = 1;
        pendingSamples = 0;
        activeKey = {};
        activePitch = static_cast<SampleType> (69);
        activeVelocity = SampleType {};
    }

    void setLegato (bool enabled) noexcept { legatoEnabled = enabled; }
    bool isLegato() const noexcept { return legatoEnabled; }

    void setPortamentoMode (PortamentoMode mode) noexcept
    {
        portamentoMode = mode;
    }

    PortamentoMode getPortamentoMode() const noexcept
    {
        return portamentoMode;
    }

    void setLegatoGraceTime (SampleType seconds) noexcept
    {
        legatoGraceSeconds = std::isfinite (seconds)
            ? std::max (SampleType {}, seconds) : SampleType {};
    }

    SampleType getLegatoGraceTime() const noexcept
    {
        return legatoGraceSeconds;
    }

    Action start (const Key& key, SampleType pitch,
                  SampleType velocity) noexcept
    {
        pendingGateOff = false;
        const auto wasFingered = findNewestSlot() >= 0;
        auto slot = findHeldSlot (key);

        if (slot < 0)
            slot = findFreeSlot();

        auto& note = held[static_cast<size_t> (slot)];
        note.key = key;
        note.pitch = sanitise (pitch);
        note.velocity = sanitise (velocity);
        note.order = nextOrder++;
        note.active = true;

        const auto wasGateActive = gateActive;
        gateActive = true;
        activeKey = note.key;
        activePitch = note.pitch;
        activeVelocity = note.velocity;

        const auto shouldTrigger = ! legatoEnabled || ! wasGateActive;
        const auto shouldGlide = shouldGlideOnStart (wasFingered);
        hasPreviousPitch = true;
        return pitchAction (note, shouldTrigger, shouldGlide);
    }

    Action stop (const Key& key) noexcept
    {
        return stopMatching (
            [&key] (const Key& candidate) noexcept
            {
                return candidate == key;
            });
    }

    template <typename Predicate>
    Action stopMatching (Predicate&& matches) noexcept
    {
        static_assert (noexcept (matches (std::declval<const Key&>())),
                       "MonoVoiceController stop predicate must be noexcept");

        auto removedActiveKey = false;
        for (auto& note : held)
            if (note.active && matches (note.key))
            {
                removedActiveKey = removedActiveKey
                                || note.key == activeKey;
                note.active = false;
            }

        if (! removedActiveKey)
            return {};

        if (const auto fallback = findNewestSlot(); fallback >= 0)
        {
            const auto& note = held[static_cast<size_t> (fallback)];
            activeKey = note.key;
            activePitch = note.pitch;
            activeVelocity = note.velocity;
            return pitchAction (note, false,
                                portamentoMode != PortamentoMode::off);
        }

        if (! gateActive || sustainActive)
        {
            pendingGateOff = false;
            return {};
        }

        if (legatoEnabled)
        {
            pendingGateOff = true;
            pendingSamples = std::max<uint64_t> (
                1, static_cast<uint64_t> (std::ceil (
                    legatoGraceSeconds * sampleRate)));
            return {};
        }

        gateActive = false;
        return releaseAction();
    }

    Action setSustain (bool active) noexcept
    {
        if (sustainActive == active)
            return {};

        sustainActive = active;

        if (sustainActive)
        {
            pendingGateOff = false;
            return {};
        }

        if (findNewestSlot() < 0 && gateActive)
        {
            pendingGateOff = false;
            gateActive = false;
            return releaseAction();
        }

        return {};
    }

    Action allKeysOff() noexcept
    {
        const auto action = gateActive ? releaseAction() : Action {};
        reset();
        return action;
    }

    Action next() noexcept
    {
        if (pendingGateOff && pendingSamples > 0 && --pendingSamples == 0)
        {
            pendingGateOff = false;
            gateActive = false;
            return releaseAction();
        }

        return {};
    }

    bool isGateActive() const noexcept { return gateActive; }
    bool hasPendingGateOff() const noexcept { return pendingGateOff; }

    size_t getHeldCount() const noexcept
    {
        size_t count = 0;

        for (const auto& note : held)
            if (note.active)
                ++count;

        return count;
    }

private:
    struct HeldNote
    {
        Key key {};
        SampleType pitch {};
        SampleType velocity {};
        uint64_t order = 0;
        bool active = false;
    };

    static SampleType sanitise (SampleType value) noexcept
    {
        return std::isfinite (value) ? value : SampleType {};
    }

    Action pitchAction (const HeldNote& note, bool trigger,
                        bool glide) const noexcept
    {
        return { Action::Type::pitch, note.key, note.pitch, note.velocity,
                 trigger, glide };
    }

    Action releaseAction() const noexcept
    {
        return { Action::Type::release, activeKey, activePitch,
                 activeVelocity, false, false };
    }

    bool shouldGlideOnStart (bool wasFingered) const noexcept
    {
        if (portamentoMode == PortamentoMode::off)
            return false;

        if (portamentoMode == PortamentoMode::always)
            return hasPreviousPitch;

        return wasFingered;
    }

    int findHeldSlot (const Key& key) const noexcept
    {
        for (size_t i = 0; i < heldCapacity; ++i)
            if (held[i].active && held[i].key == key)
                return static_cast<int> (i);

        return -1;
    }

    int findNewestSlot() const noexcept
    {
        int newest = -1;
        uint64_t newestOrder = 0;

        for (size_t i = 0; i < heldCapacity; ++i)
            if (held[i].active && held[i].order >= newestOrder)
            {
                newest = static_cast<int> (i);
                newestOrder = held[i].order;
            }

        return newest;
    }

    int findFreeSlot() const noexcept
    {
        for (size_t i = 0; i < heldCapacity; ++i)
            if (! held[i].active)
                return static_cast<int> (i);

        size_t oldest = 0;

        for (size_t i = 1; i < heldCapacity; ++i)
            if (held[i].order < held[oldest].order)
                oldest = i;

        return static_cast<int> (oldest);
    }

    std::array<HeldNote, heldCapacity> held {};
    SampleType sampleRate = static_cast<SampleType> (48000);
    SampleType legatoGraceSeconds = defaultLegatoGraceSeconds;
    Key activeKey {};
    SampleType activePitch = static_cast<SampleType> (69);
    SampleType activeVelocity {};
    uint64_t nextOrder = 1;
    uint64_t pendingSamples = 0;
    PortamentoMode portamentoMode = PortamentoMode::off;
    bool legatoEnabled = false;
    bool sustainActive = false;
    bool gateActive = false;
    bool pendingGateOff = false;
    bool hasPreviousPitch = false;
};

} // namespace chardsp
