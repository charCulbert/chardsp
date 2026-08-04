#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "choc/audio/io/choc_AudioMIDIPlayer.h"
#include "choc/containers/choc_SingleReaderSingleWriterFIFO.h"

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
    ActionQueue()
    {
        fifo.reset (capacity);
    }

    bool push (Action action) noexcept
    {
        return fifo.push (std::move (action));
    }

    bool pop (Action &action) noexcept
    {
        return fifo.pop (action);
    }

private:
    choc::fifo::SingleReaderSingleWriterFIFO<Action> fifo;
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
