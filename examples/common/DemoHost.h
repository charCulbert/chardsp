#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "choc/audio/io/choc_AudioMIDIPlayer.h"

namespace chardsp::examples
{
class Demo : public choc::audio::io::AudioMIDICallback
{
public:
    virtual std::string_view getName () const noexcept = 0;
    virtual std::string_view getPageConfig () const noexcept = 0;
    virtual bool performAction (std::string_view, int) noexcept
    {
        return false;
    }
    virtual void setControl (std::string_view, float) noexcept {}

    void startBlock () override {}
    void endBlock () override {}
};

template <typename Action, size_t capacity = 64> class ActionQueue
{
public:
    bool push (Action action) noexcept
    {
        const auto nextWrite = write.load (std::memory_order_relaxed);
        const auto nextRead = read.load (std::memory_order_acquire);

        if (nextWrite - nextRead >= capacity)
            return false;

        actions[nextWrite % capacity] = action;
        write.store (nextWrite + 1, std::memory_order_release);
        return true;
    }

    bool pop (Action &action) noexcept
    {
        const auto nextRead = read.load (std::memory_order_relaxed);
        const auto nextWrite = write.load (std::memory_order_acquire);

        if (nextRead == nextWrite)
            return false;

        action = actions[nextRead % capacity];
        read.store (nextRead + 1, std::memory_order_release);
        return true;
    }

private:
    std::array<Action, capacity> actions{};
    std::atomic<uint32_t> write{0};
    std::atomic<uint32_t> read{0};
};

template <typename NextSample>
void renderMono (const choc::audio::AudioMIDIBlockDispatcher::Block &block, bool replaceOutput,
                 NextSample &&nextSample) noexcept
{
    for (choc::buffer::FrameCount frame = 0; frame < block.audioOutput.getNumFrames (); ++frame)
    {
        const auto sample = nextSample ();

        for (choc::buffer::ChannelCount channel = 0; channel < block.audioOutput.getNumChannels ();
             ++channel)
        {
            auto &output = block.audioOutput.getSample (channel, frame);
            output = replaceOutput ? sample : output + sample;
        }
    }
}

int runDemo (Demo &);
} // namespace chardsp::examples
