#include "../common/DemoHost.h"

#include <algorithm>
#include <atomic>

#include "chardsp/chardsp_BasicSynthVoice.h"
#include "chardsp/chardsp_VoicePool.h"

namespace
{
struct Voice
{
    void prepare (float rate, size_t index) noexcept
    {
        voice.prepare (rate, index);
    }
    void start (const int &note, float velocity) noexcept
    {
        voice.start (static_cast<float> (note), velocity);
    }
    void release () noexcept
    {
        voice.release ();
    }
    void forceRelease (float seconds) noexcept
    {
        voice.forceRelease (seconds);
    }
    bool isAvailable () const noexcept
    {
        return voice.isAvailable ();
    }
    float next () noexcept
    {
        return voice.next ();
    }
    chardsp::BasicSynthVoice<float> voice;
};

class BasicSynthDemo final : public chardsp::examples::Demo
{
public:
    std::string_view getName () const noexcept override
    {
        return "Polyphonic basic synth";
    }
    std::string_view getPageConfig () const noexcept override
    {
        return R"JS({title:"Polyphonic basic synth",description:"Play eight BasicSynthVoice instances through VoicePool. Choose both oscillator shapes, then adjust mix, pulse width when relevant, noise, ADSR, filter, and velocity response.",controls:[{name:"velocity",label:"Played-note velocity",min:0,max:1,step:.01,value:.8,precision:2,section:"Input"},{name:"osc1Level",label:"Oscillator 1 level",min:0,max:1,step:.01,value:.7,precision:2,section:"Voice mix"},{name:"osc1Pulse",label:"Oscillator 1 pulse width",min:.02,max:.98,step:.01,value:.5,precision:2,when:{group:"osc1",values:[1]}},{name:"osc2Level",label:"Oscillator 2 level",min:0,max:1,step:.01,value:.3,precision:2},{name:"osc2Pulse",label:"Oscillator 2 pulse width",min:.02,max:.98,step:.01,value:.5,precision:2,when:{group:"osc2",values:[1]}},{name:"noise",label:"Noise level",min:0,max:.3,step:.01,value:.02,precision:2},{name:"attack",label:"Attack",min:0,max:2,step:.01,value:.01,unit:" s",precision:2,section:"Envelope"},{name:"decay",label:"Decay",min:0,max:2,step:.01,value:.18,unit:" s",precision:2},{name:"sustain",label:"Sustain level",min:0,max:1,step:.01,value:.62,precision:2},{name:"release",label:"Release",min:0,max:3,step:.01,value:.6,unit:" s",precision:2},{name:"cutoff",label:"Filter cutoff",min:20,max:20000,step:10,value:4000,unit:" Hz",section:"Filter"},{name:"q",label:"Resonance (Q)",min:.1,max:10,step:.1,value:.8,precision:1},{name:"velocityAmount",label:"Velocity amount",min:0,max:1,step:.01,value:1,precision:2,section:"Expression"},{name:"velocityExponent",label:"Velocity curve exponent (1 = linear)",min:.1,max:4,step:.1,value:1,precision:1}],actions:[{label:"Saw",action:"osc1",value:0,group:"osc1",groupLabel:"Oscillator 1 shape",selected:true},{label:"Pulse",action:"osc1",value:1,group:"osc1"},{label:"Triangle",action:"osc1",value:2,group:"osc1"},{label:"Sine",action:"osc1",value:3,group:"osc1"},{label:"Saw",action:"osc2",value:0,group:"osc2",groupLabel:"Oscillator 2 shape"},{label:"Pulse",action:"osc2",value:1,group:"osc2"},{label:"Triangle",action:"osc2",value:2,group:"osc2"},{label:"Sine",action:"osc2",value:3,group:"osc2",selected:true},{label:"Brown",action:"noiseColor",value:0,group:"noiseColor",groupLabel:"Noise colour"},{label:"Pink",action:"noiseColor",value:1,group:"noiseColor"},{label:"White",action:"noiseColor",value:2,group:"noiseColor",selected:true},{label:"Blue",action:"noiseColor",value:3,group:"noiseColor"},{label:"Exponential",action:"envelopeShape",value:0,group:"envelopeShape",groupLabel:"Envelope shape",selected:true},{label:"Linear",action:"envelopeShape",value:1,group:"envelopeShape"},{label:"Lowpass",action:"filterMode",value:0,group:"filterMode",groupLabel:"Filter mode",selected:true},{label:"Bandpass",action:"filterMode",value:1,group:"filterMode"},{label:"Highpass",action:"filterMode",value:2,group:"filterMode"},{label:"Release all notes",action:"releaseAll",clearNotes:true}],notes:{on:"noteOn",off:"noteOff",root:48,count:37}})JS";
    }
    void sampleRateChanged (double rate) override
    {
        sampleRate = static_cast<float> (rate);
        pool.prepare (sampleRate);
        pool.setStealFadeTime (.002f);
        configureDefaults ();
    }
    void setControl (std::string_view name, float value) noexcept override
    {
        if (name == "velocity")
            velocity.store (std::clamp (value, 0.0f, 1.0f));
        else if (name == "osc1Level")
            osc1Level.store (std::clamp (value, 0.0f, 1.0f));
        else if (name == "osc2Level")
            osc2Level.store (std::clamp (value, 0.0f, 1.0f));
        else if (name == "osc1Pulse")
            osc1Pulse.store (std::clamp (value, .02f, .98f));
        else if (name == "osc2Pulse")
            osc2Pulse.store (std::clamp (value, .02f, .98f));
        else if (name == "noise")
            noise.store (std::clamp (value, 0.0f, .3f));
        else if (name == "attack")
            attack.store (std::clamp (value, 0.0f, 2.0f));
        else if (name == "decay")
            decay.store (std::clamp (value, 0.0f, 2.0f));
        else if (name == "sustain")
            sustain.store (std::clamp (value, 0.0f, 1.0f));
        else if (name == "release")
            release.store (std::clamp (value, 0.0f, 3.0f));
        else if (name == "cutoff")
            cutoff.store (std::clamp (value, 20.0f, 20000.0f));
        else if (name == "q")
            q.store (std::clamp (value, .1f, 10.0f));
        else if (name == "velocityAmount")
            velocityAmount.store (std::clamp (value, 0.0f, 1.0f));
        else if (name == "velocityExponent")
            velocityExponent.store (std::clamp (value, .1f, 4.0f));
    }
    bool performAction (std::string_view name, int value) noexcept override
    {
        if (name == "noteOn")
            return actions.push ({0, value});
        if (name == "noteOff")
            return actions.push ({1, value});
        if (name == "osc1")
            return actions.push ({2, value});
        if (name == "osc2")
            return actions.push ({3, value});
        if (name == "noiseColor")
            return actions.push ({4, value});
        if (name == "envelopeShape")
            return actions.push ({5, value});
        if (name == "filterMode")
            return actions.push ({6, value});
        if (name == "releaseAll")
            return actions.push ({7, 0});
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
                pool.start (static_cast<int> (message.getNoteNumber ()),
                            static_cast<float> (message.getVelocity ()) / 127.0f);
            else if (message.isNoteOff ())
                pool.stop (static_cast<int> (message.getNoteNumber ()));
            else if (message.isAllNotesOff ())
                pool.releaseAll ();
            else if (message.isAllSoundOff ())
                pool.reset ();
        }
        applyControls ();
        Action action{};
        while (actions.pop (action))
        {
            if (action.type == 0)
                pool.start (action.value, velocity.load ());
            else if (action.type == 1)
                pool.stop (action.value);
            else if (action.type == 7)
                pool.releaseAll ();
            else
            {
                if (action.type == 6)
                {
                    filterMode = static_cast<chardsp::FilterMode> (std::clamp (action.value, 0, 2));
                    continue;
                }

                forEachVoice ([&] (auto &voice) {
                    if (action.type == 2)
                        voice.setOscillator1Waveform (static_cast<chardsp::BasicSynthWaveform> (
                            std::clamp (action.value, 0, 3)));
                    else if (action.type == 3)
                        voice.setOscillator2Waveform (static_cast<chardsp::BasicSynthWaveform> (
                            std::clamp (action.value, 0, 3)));
                    else if (action.type == 4)
                        voice.setNoiseColor (
                            static_cast<chardsp::NoiseColor> (std::clamp (action.value, 0, 3)));
                    else if (action.type == 5)
                        voice.setEnvelopeShape (action.value == 0
                                                    ? chardsp::EnvelopeShape::exponential
                                                    : chardsp::EnvelopeShape::linear);
                });
            }
        }
        chardsp::examples::renderMono (block, replaceOutput,
                                       [this] { return pool.next () * .25f; });
    }

private:
    struct Action
    {
        int type;
        int value;
    };
    template <typename Function> void forEachVoice (Function &&function)
    {
        for (size_t i = 0; i < 8; ++i)
            function (pool.getVoice (i).voice);
    }
    void configureDefaults ()
    {
        forEachVoice ([] (auto &voice) {
            voice.setOscillator1Waveform (chardsp::BasicSynthWaveform::saw);
            voice.setOscillator2Waveform (chardsp::BasicSynthWaveform::sine);
            voice.setNoiseColor (chardsp::NoiseColor::white);
            voice.setEnvelopeShape (chardsp::EnvelopeShape::exponential);
        });
        applyControls ();
    }
    void applyControls ()
    {
        forEachVoice ([this] (auto &voice) {
            voice.setOscillator1Level (osc1Level.load ());
            voice.setOscillator2Level (osc2Level.load ());
            voice.setOscillator1PulseWidth (osc1Pulse.load ());
            voice.setOscillator2PulseWidth (osc2Pulse.load ());
            voice.setNoiseLevel (noise.load ());
            voice.setAttack (attack.load ());
            voice.setDecay (decay.load ());
            voice.setSustain (sustain.load ());
            voice.setRelease (release.load ());
            voice.setFilter (filterMode, cutoff.load (), q.load ());
            voice.setVelocitySensitivity (velocityAmount.load ());
            voice.setVelocityCurve (velocityExponent.load ());
        });
    }
    chardsp::examples::ActionQueue<Action> actions;
    std::atomic<float> velocity{.8f}, osc1Level{.7f}, osc2Level{.3f}, osc1Pulse{.5f},
        osc2Pulse{.5f}, noise{.02f}, attack{.01f}, decay{.18f}, sustain{.62f}, release{.6f},
        cutoff{4000}, q{.8f}, velocityAmount{1}, velocityExponent{1};
    float sampleRate = 48000;
    chardsp::FilterMode filterMode = chardsp::FilterMode::lowpass;
    chardsp::VoicePool<Voice, 8, int> pool;
};
} // namespace
int main ()
{
    BasicSynthDemo demo;
    return chardsp::examples::runDemo (demo);
}
