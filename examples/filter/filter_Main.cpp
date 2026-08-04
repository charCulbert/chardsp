#include "../common/DemoHost.h"

#include <algorithm>
#include <atomic>

#include "chardsp/chardsp_Oscillator.h"
#include "chardsp/chardsp_StateVariableFilter.h"

namespace
{
class FilterDemo final : public chardsp::examples::Demo
{
public:
    std::string_view getName () const noexcept override
    {
        return "State-variable filter";
    }
    std::string_view getPageConfig () const noexcept override
    {
        return R"JS({title:"State-variable filter",description:"Compare direct, lowpass, bandpass, highpass, and notch outputs.",controls:[{name:"frequency",label:"Saw frequency",min:40,max:2000,step:1,value:110,unit:" Hz"},{name:"cutoff",label:"Cutoff",min:20,max:20000,step:10,value:1200,unit:" Hz",section:"Filter"},{name:"q",label:"Resonance (Q)",min:.1,max:10,step:.1,value:1.2,precision:1}],actions:[{label:"Bypass",action:"mode",value:0,group:"mode",groupLabel:"Output"},{label:"Lowpass",action:"mode",value:1,group:"mode",selected:true},{label:"Bandpass",action:"mode",value:2,group:"mode"},{label:"Highpass",action:"mode",value:3,group:"mode"},{label:"Notch",action:"mode",value:4,group:"mode"},{label:"Off",action:"enabled",value:0,group:"enabled",groupLabel:"Tone"},{label:"On",action:"enabled",value:1,group:"enabled",selected:true}]})JS";
    }
    void sampleRateChanged (double rate) override
    {
        oscillator.prepare (static_cast<float> (rate));
        oscillator.setFrequency (110);
        filter.prepare (static_cast<float> (rate));
        filter.setMode (chardsp::FilterMode::lowpass);
    }
    void setControl (std::string_view name, float value) noexcept override
    {
        if (name == "frequency")
            frequency.store (std::clamp (value, 40.0f, 2000.0f));
        else if (name == "cutoff")
            cutoff.store (std::clamp (value, 20.0f, 20000.0f));
        else if (name == "q")
            q.store (std::clamp (value, .1f, 10.0f));
    }
    bool performAction (std::string_view name, int value) noexcept override
    {
        if (name == "mode")
            return actions.push ({0, value});
        if (name == "enabled")
            return actions.push ({1, value});
        return false;
    }
    void processSubBlock (const choc::audio::AudioMIDIBlockDispatcher::Block &block,
                          bool replaceOutput) override
    {
        Action action{};
        while (actions.pop (action))
        {
            if (action.type == 1)
                enabled = action.value != 0;
            else
            {
                output = std::clamp (action.value, 0, 4);
                filter.reset ();
                if (output > 0 && output < 4)
                    filter.setMode (static_cast<chardsp::FilterMode> (output - 1));
            }
        }
        oscillator.setFrequency (frequency.load ());
        filter.setCutoffAndQ (cutoff.load (), q.load ());
        chardsp::examples::renderMono (block, replaceOutput, [this] {
            if (!enabled)
                return 0.0f;
            const auto input = oscillator.next ();
            if (output == 0)
                return input * .2f;
            if (output == 4)
                return filter.processAll (input).notch * .2f;
            return filter.process (input) * .2f;
        });
    }

private:
    struct Action
    {
        int type;
        int value;
    };
    chardsp::examples::ActionQueue<Action> actions;
    std::atomic<float> frequency{110}, cutoff{1200}, q{1.2f};
    bool enabled = true;
    int output = 1;
    chardsp::EllipticSawOscillator<float> oscillator;
    chardsp::StateVariableFilter<float> filter;
};
} // namespace
int main ()
{
    FilterDemo demo;
    return chardsp::examples::runDemo (demo);
}
