#include "../common/DemoHost.h"

#include <algorithm>
#include <atomic>

#include "chardsp/chardsp_BasicSynthVoice.h"
#include "chardsp/chardsp_MonoVoiceController.h"
#include "chardsp/chardsp_Portamento.h"

namespace
{
class MonoPortamentoDemo final : public chardsp::examples::Demo
{
public:
    std::string_view getName () const noexcept override
    {
        return "Mono portamento";
    }
    std::string_view getPageConfig () const noexcept override
    {
        return R"JS({title:"Mono portamento",description:"Hold overlapping notes to test legato retriggering, glide modes, newest-note priority, release fallback, sustain, and note-off grace time.",controls:[{name:"portamento",label:"Base glide time",min:0,max:1,step:.01,value:.18,unit:" s",precision:2,section:"Portamento"},{name:"shape",label:"Curve (linear–exponential)",min:0,max:1,step:.01,value:.5,precision:2},{name:"timeRate",label:"Time scaling (constant time–constant rate)",min:0,max:1,step:.01,value:.5,precision:2},{name:"grace",label:"Note-off grace time",min:0,max:.25,step:.001,value:.024,unit:" ms",multiplier:1000,section:"Note policy"},{name:"release",label:"Voice release time",min:0,max:3,step:.01,value:.35,unit:" s",precision:2,section:"Voice"},{name:"velocity",label:"Note-on velocity",min:0,max:1,step:.01,value:.8,precision:2}],actions:[{label:"On",action:"legato",value:1,group:"legato",groupLabel:"Legato",selected:true},{label:"Off",action:"legato",value:0,group:"legato"},{label:"Off",action:"mode",value:0,group:"mode",groupLabel:"Portamento"},{label:"Always",action:"mode",value:1,group:"mode",selected:true},{label:"Fingered",action:"mode",value:2,group:"mode"},{label:"Off",action:"sustain",value:0,group:"sustain",groupLabel:"Sustain",selected:true},{label:"On",action:"sustain",value:1,group:"sustain"},{label:"Release all notes",action:"releaseAll",clearNotes:true}],notes:{on:"noteOn",off:"noteOff",root:60,count:25}})JS";
    }
    void sampleRateChanged (double rate) override
    {
        sampleRate = static_cast<float> (rate);
        voice.prepare (sampleRate, 0);
        voice.setOscillator1Level (0);
        voice.setOscillator2Waveform (chardsp::BasicSynthWaveform::sine);
        voice.setOscillator2Level (1);
        voice.setNoiseLevel (0);
        voice.setAttack (.01f);
        voice.setRelease (.35f);
        voice.setFilter (chardsp::FilterMode::lowpass, sampleRate * .48f, .707f);
        controller.prepare (sampleRate);
        controller.setLegato (true);
        controller.setPortamentoMode (chardsp::PortamentoMode::always);
        portamento.prepare (sampleRate);
        portamento.reset (57);
    }
    void setControl (std::string_view name, float value) noexcept override
    {
        if (name == "portamento")
            time.store (std::clamp (value, 0.0f, 1.0f));
        else if (name == "shape")
            shape.store (std::clamp (value, 0.0f, 1.0f));
        else if (name == "timeRate")
            timeRate.store (std::clamp (value, 0.0f, 1.0f));
        else if (name == "grace")
            grace.store (std::clamp (value, 0.0f, .25f));
        else if (name == "release")
            release.store (std::clamp (value, 0.0f, 3.0f));
        else if (name == "velocity")
            velocity.store (std::clamp (value, 0.0f, 1.0f));
    }
    bool performAction (std::string_view name, int value) noexcept override
    {
        if (name == "noteOn")
            return actions.push ({0, value});
        if (name == "noteOff")
            return actions.push ({1, value});
        if (name == "legato")
            return actions.push ({2, value});
        if (name == "mode")
            return actions.push ({3, value});
        if (name == "sustain")
            return actions.push ({4, value});
        if (name == "releaseAll")
            return actions.push ({5, 0});
        return false;
    }
    void processSubBlock (const choc::audio::AudioMIDIBlockDispatcher::Block &block,
                          bool replaceOutput) override
    {
        for (const auto &midi : block.midiMessages)
        {
            const auto message = midi.message;
            if (message.length () < 3)
                continue;
            if (message.isNoteOn ())
                apply (controller.start (static_cast<int> (message.getNoteNumber ()),
                                         static_cast<float> (message.getNoteNumber ()),
                                         static_cast<float> (message.getVelocity ()) / 127.0f));
            else if (message.isNoteOff ())
                apply (controller.stop (static_cast<int> (message.getNoteNumber ())));
            else if (message.isAllNotesOff ())
                apply (controller.allKeysOff ());
        }
        portamento.setTime (time.load ());
        portamento.setShape (shape.load ());
        portamento.setTimeRateMorph (timeRate.load ());
        controller.setLegatoGraceTime (grace.load ());
        voice.setRelease (release.load ());
        Action action{};
        while (actions.pop (action))
        {
            if (action.type == 0)
                apply (controller.start (action.value, static_cast<float> (action.value),
                                         velocity.load ()));
            else if (action.type == 1)
                apply (controller.stop (action.value));
            else if (action.type == 2)
                controller.setLegato (action.value != 0);
            else if (action.type == 3)
                controller.setPortamentoMode (
                    static_cast<chardsp::PortamentoMode> (std::clamp (action.value, 0, 2)));
            else if (action.type == 4)
                apply (controller.setSustain (action.value != 0));
            else
                apply (controller.allKeysOff ());
        }
        chardsp::examples::renderMono (block, replaceOutput, [this] {
            apply (controller.next ());
            voice.setPitch (portamento.next ());
            return voice.next () * .18f;
        });
    }

private:
    using Controller = chardsp::MonoVoiceController<float, int, 4>;
    struct Action
    {
        int type;
        int value;
    };
    void apply (const Controller::Action &action)
    {
        if (action.hasPitch ())
        {
            portamento.setTarget (action.pitch, action.glide);
            if (action.trigger)
                voice.start (action.pitch, action.velocity);
        }
        else if (action.shouldRelease ())
            voice.release ();
    }
    chardsp::examples::ActionQueue<Action> actions;
    std::atomic<float> time{.18f}, shape{.5f}, timeRate{.5f}, grace{.024f}, release{.35f},
        velocity{.8f};
    float sampleRate = 48000;
    Controller controller;
    chardsp::Portamento<float> portamento;
    chardsp::BasicSynthVoice<float> voice;
};
} // namespace
int main ()
{
    MonoPortamentoDemo demo;
    return chardsp::examples::runDemo (demo);
}
