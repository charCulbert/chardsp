#include "../common/DemoHost.h"

#include <algorithm>
#include <atomic>
#include <cstdint>

#include "chardsp/chardsp_DelayLine.h"
#include "chardsp/chardsp_Oscillator.h"

namespace
{
class DelayDemo final : public chardsp::examples::Demo
{
public:
    std::string_view getName () const noexcept override
    {
        return "Delay line";
    }
    std::string_view getPageConfig () const noexcept override
    {
        return R"JS({title:"Delay line",description:"Inject sine bursts, then change delay time, feedback, and wet mix.",controls:[{name:"frequency",label:"Burst frequency",min:40,max:2000,step:1,value:220,unit:" Hz"},{name:"burst",label:"Burst length",min:.01,max:.5,step:.01,value:.1,unit:" s",precision:2},{name:"delay",label:"Delay time",min:0,max:.95,step:.001,value:.25,unit:" s",precision:3},{name:"feedback",label:"Feedback amount",min:0,max:.9,step:.01,value:.55,precision:2},{name:"wet",label:"Wet mix",min:0,max:1,step:.01,value:.7,precision:2}],actions:[{label:"Trigger burst",action:"trigger"},{label:"Clear delay",action:"clear"}]})JS";
    }
    void sampleRateChanged (double rate) override
    {
        sampleRate = static_cast<float> (rate);
        oscillator.prepare (sampleRate);
        oscillator.setFrequency (220);
        delay.reset ();
        delay.setDelay (sampleRate * .25f);
    }
    void setControl (std::string_view name, float value) noexcept override
    {
        if (name == "frequency")
            frequency.store (std::clamp (value, 40.0f, 2000.0f));
        else if (name == "burst")
            burst.store (std::clamp (value, .01f, .5f));
        else if (name == "delay")
            delayTime.store (std::clamp (value, 0.0f, .95f));
        else if (name == "feedback")
            feedback.store (std::clamp (value, 0.0f, .9f));
        else if (name == "wet")
            wet.store (std::clamp (value, 0.0f, 1.0f));
    }
    bool performAction (std::string_view name, int) noexcept override
    {
        if (name == "trigger")
            return actions.push (0);
        if (name == "clear")
            return actions.push (1);
        return false;
    }
    void processSubBlock (const choc::audio::AudioMIDIBlockDispatcher::Block &block,
                          bool replaceOutput) override
    {
        int action = 0;
        while (actions.pop (action))
        {
            if (action == 0)
            {
                remaining =
                    std::max<uint64_t> (1, static_cast<uint64_t> (sampleRate * burst.load ()));
                oscillator.reset ();
            }
            else
            {
                remaining = 0;
                delay.reset ();
            }
        }
        oscillator.setFrequency (frequency.load ());
        delay.setDelay (delayTime.load () * sampleRate);
        chardsp::examples::renderMono (block, replaceOutput, [this] {
            const auto dry = remaining > 0 ? oscillator.next () : 0.0f;
            if (remaining > 0)
                --remaining;
            const auto delayed = delay.read ();
            delay.write (dry + delayed * feedback.load ());
            return (dry * (1 - wet.load ()) + delayed * wet.load ()) * .25f;
        });
    }

private:
    chardsp::examples::ActionQueue<int> actions;
    std::atomic<float> frequency{220}, burst{.1f}, delayTime{.25f}, feedback{.55f}, wet{.7f};
    float sampleRate = 48000;
    uint64_t remaining = 0;
    chardsp::SineOscillator<float> oscillator;
    chardsp::DelayLine<float, 48000> delay;
};
} // namespace
int main ()
{
    DelayDemo demo;
    return chardsp::examples::runDemo (demo);
}
