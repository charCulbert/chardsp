#include "../common/DemoHost.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

#include "chardsp/chardsp_Oscillator.h"
#include "chardsp/chardsp_Oversampler.h"

namespace
{
class OversamplerDemo final : public chardsp::examples::Demo
{
public:
    std::string_view getName () const noexcept override
    {
        return "Block oversampler";
    }
    std::string_view getPageConfig () const noexcept override
    {
        return R"JS({title:"Block oversampler",description:"Compare the same driven sine through 1x, 2x, and 4x block processing.",controls:[{name:"frequency",label:"Input frequency",min:100,max:12000,step:10,value:9000,unit:" Hz"},{name:"drive",label:"Waveshaper drive",min:1,max:20,step:.1,value:3,unit:" x",precision:1}],actions:[{label:"1x direct",action:"mode",value:0,group:"mode",groupLabel:"Rate"},{label:"2x oversampled",action:"mode",value:1,group:"mode"},{label:"4x oversampled",action:"mode",value:2,group:"mode",selected:true},{label:"Reset phase + FIR",action:"reset"},{label:"Off",action:"enabled",value:0,group:"enabled",groupLabel:"Output"},{label:"On",action:"enabled",value:1,group:"enabled",selected:true}]})JS";
    }
    void sampleRateChanged (double rate) override
    {
        oscillator.prepare (static_cast<float> (rate));
        two.prepare (1, maxFrames, chardsp::OversamplingFactor::x2);
        four.prepare (1, maxFrames, chardsp::OversamplingFactor::x4);
        reset ();
    }
    void setControl (std::string_view name, float value) noexcept override
    {
        if (name == "frequency")
            frequency.store (std::clamp (value, 100.0f, 12000.0f));
        else if (name == "drive")
            drive.store (std::clamp (value, 1.0f, 20.0f));
    }
    bool performAction (std::string_view name, int value) noexcept override
    {
        if (name == "mode")
            return actions.push ({0, value});
        if (name == "enabled")
            return actions.push ({1, value});
        if (name == "reset")
            return actions.push ({2, 0});
        return false;
    }
    void processSubBlock (const choc::audio::AudioMIDIBlockDispatcher::Block &block,
                          bool replaceOutput) override
    {
        Action action{};
        while (actions.pop (action))
        {
            if (action.type == 0)
            {
                mode = std::clamp (action.value, 0, 2);
                reset ();
            }
            else if (action.type == 1)
                enabled = action.value != 0;
            else
                reset ();
        }
        oscillator.setFrequency (frequency.load ());
        const auto frames = block.audioOutput.getNumFrames ();
        const auto amount = drive.load ();
        if (!enabled || mode == 0 || frames > maxFrames)
        {
            chardsp::examples::renderMono (block, replaceOutput, [this, amount] {
                return enabled ? std::tanh (oscillator.next () * amount) * .2f : 0.0f;
            });
            return;
        }
        for (uint32_t frame = 0; frame < frames; ++frame)
            input[frame] = oscillator.next ();
        std::array<const float *, 1> inputChannels{input.data ()};
        std::array<float *, 1> outputChannels{output.data ()};
        auto &processor = mode == 1 ? two : four;
        auto highRate = processor.upsample (inputChannels.data (), 1, frames);
        const auto highRateFrames = frames * static_cast<uint32_t> (processor.getFactor ());
        for (uint32_t frame = 0; frame < highRateFrames; ++frame)
            highRate[0][frame] = std::tanh (highRate[0][frame] * amount) * .2f;
        processor.downsample (outputChannels.data (), 1, frames);
        uint32_t frame = 0;
        chardsp::examples::renderMono (block, replaceOutput,
                                       [this, &frame] { return output[frame++]; });
    }

private:
    void reset ()
    {
        oscillator.reset ();
        two.reset ();
        four.reset ();
    }
    static constexpr uint32_t maxFrames = 2048;
    struct Action
    {
        int type;
        int value;
    };
    chardsp::examples::ActionQueue<Action> actions;
    std::atomic<float> frequency{9000}, drive{3};
    int mode = 2;
    bool enabled = true;
    chardsp::SineOscillator<float> oscillator;
    chardsp::Oversampler<float> two, four;
    std::array<float, maxFrames> input{}, output{};
};
} // namespace
int main ()
{
    OversamplerDemo demo;
    return chardsp::examples::runDemo (demo);
}
