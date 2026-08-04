#include "../common/DemoHost.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

#include "chardsp/chardsp_Downsampler4x.h"
#include "chardsp/chardsp_Oscillator.h"

namespace
{
class DownsamplerDemo final : public chardsp::examples::Demo
{
public:
    std::string_view getName () const noexcept override
    {
        return "4x downsampler";
    }
    std::string_view getPageConfig () const noexcept override
    {
        return R"JS({title:"4x downsampler",description:"Compare a driven sine directly at 1x with external 4x rendering through Downsampler4x.",controls:[{name:"frequency",label:"Input frequency",min:100,max:12000,step:10,value:9000,unit:" Hz"},{name:"drive",label:"Waveshaper drive",min:1,max:20,step:.1,value:3,unit:" x",precision:1}],actions:[{label:"1x direct (aliased)",action:"mode",value:0,group:"mode",groupLabel:"Path"},{label:"4x + Downsampler4x",action:"mode",value:1,group:"mode",selected:true},{label:"Reset phase + filter",action:"reset"},{label:"Off",action:"enabled",value:0,group:"enabled",groupLabel:"Output"},{label:"On",action:"enabled",value:1,group:"enabled",selected:true}]})JS";
    }
    void sampleRateChanged (double rate) override
    {
        sampleRate = static_cast<float> (rate);
        low.prepare (sampleRate);
        high.prepare (sampleRate * 4);
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
                mode = std::clamp (action.value, 0, 1);
                reset ();
            }
            else if (action.type == 1)
                enabled = action.value != 0;
            else
                reset ();
        }
        const auto hz = frequency.load ();
        low.setFrequency (hz);
        high.setFrequency (hz);
        chardsp::examples::renderMono (block, replaceOutput, [this] {
            if (!enabled)
                return 0.0f;
            const auto amount = drive.load ();
            if (mode == 0)
                return std::tanh (low.next () * amount) * .2f;
            std::array<float, 4> samples{};
            for (auto &sample : samples)
                sample = std::tanh (high.next () * amount) * .2f;
            return downsampler.process (samples);
        });
    }

private:
    void reset ()
    {
        low.reset ();
        high.reset ();
        downsampler.reset ();
    }
    struct Action
    {
        int type;
        int value;
    };
    chardsp::examples::ActionQueue<Action> actions;
    std::atomic<float> frequency{9000}, drive{3};
    float sampleRate = 48000;
    int mode = 1;
    bool enabled = true;
    chardsp::SineOscillator<float> low, high;
    chardsp::Downsampler4x<float> downsampler;
};
} // namespace
int main ()
{
    DownsamplerDemo demo;
    return chardsp::examples::runDemo (demo);
}
