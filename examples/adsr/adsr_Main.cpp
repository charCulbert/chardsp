#include "../common/DemoHost.h"

#include <algorithm>
#include <atomic>

#include "chardsp/chardsp_ADSREnvelope.h"
#include "chardsp/chardsp_Oscillator.h"

namespace
{
class ADSRDemo final : public chardsp::examples::Demo
{
public:
    std::string_view getName () const noexcept override
    {
        return "ADSR envelope";
    }
    std::string_view getPageConfig () const noexcept override
    {
        return R"JS({title:"ADSR envelope",description:"Trigger starts from zero, retrigger starts from the current level, and release follows the selected curve.",controls:[{name:"frequency",label:"Sine frequency",min:40,max:2000,step:1,value:220,unit:" Hz"},{name:"attack",label:"Attack",min:0,max:2,step:.01,value:.15,unit:" s",precision:2,section:"Envelope"},{name:"decay",label:"Decay",min:0,max:2,step:.01,value:.25,unit:" s",precision:2},{name:"sustain",label:"Sustain level",min:0,max:1,step:.01,value:.45,precision:2},{name:"release",label:"Release",min:0,max:3,step:.01,value:.55,unit:" s",precision:2}],actions:[{label:"Trigger note",action:"trigger"},{label:"Retrigger current level",action:"retrigger"},{label:"Release note",action:"release"},{label:"Exponential",action:"shape",value:0,group:"shape",groupLabel:"Curve",selected:true},{label:"Linear",action:"shape",value:1,group:"shape"}]})JS";
    }
    void sampleRateChanged (double rate) override
    {
        oscillator.prepare (static_cast<float> (rate));
        oscillator.setFrequency (220.0f);
        envelope.prepare (static_cast<float> (rate));
        envelope.setShape (chardsp::EnvelopeShape::exponential);
    }
    void setControl (std::string_view name, float value) noexcept override
    {
        if (name == "frequency")
            frequency.store (std::clamp (value, 40.0f, 2000.0f));
        else if (name == "attack")
            attack.store (std::clamp (value, 0.0f, 2.0f));
        else if (name == "decay")
            decay.store (std::clamp (value, 0.0f, 2.0f));
        else if (name == "sustain")
            sustain.store (std::clamp (value, 0.0f, 1.0f));
        else if (name == "release")
            release.store (std::clamp (value, 0.0f, 3.0f));
    }
    bool performAction (std::string_view name, int value) noexcept override
    {
        if (name == "trigger")
            return actions.push ({0, 0});
        if (name == "retrigger")
            return actions.push ({1, 0});
        if (name == "release")
            return actions.push ({2, 0});
        if (name == "shape")
            return actions.push ({3, value});
        return false;
    }
    void processSubBlock (const choc::audio::AudioMIDIBlockDispatcher::Block &block,
                          bool replaceOutput) override
    {
        Action action{};
        while (actions.pop (action))
            if (action.type == 0)
                envelope.trigger ();
            else if (action.type == 1)
                envelope.retrigger ();
            else if (action.type == 2)
                envelope.release ();
            else
                envelope.setShape (action.value == 0 ? chardsp::EnvelopeShape::exponential
                                                     : chardsp::EnvelopeShape::linear);
        oscillator.setFrequency (frequency.load ());
        envelope.setAttack (attack.load ());
        envelope.setDecay (decay.load ());
        envelope.setSustain (sustain.load ());
        envelope.setRelease (release.load ());
        chardsp::examples::renderMono (
            block, replaceOutput, [this] { return oscillator.next () * envelope.next () * 0.25f; });
    }

private:
    struct Action
    {
        int type;
        int value;
    };
    chardsp::examples::ActionQueue<Action> actions;
    std::atomic<float> frequency{220.0f}, attack{0.15f}, decay{0.25f}, sustain{0.45f},
        release{0.55f};
    chardsp::SineOscillator<float> oscillator;
    chardsp::ADSREnvelope<float> envelope;
};
} // namespace
int main ()
{
    ADSRDemo demo;
    return chardsp::examples::runDemo (demo);
}
