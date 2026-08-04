#include "../common/DemoHost.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <variant>

#include "chardsp/chardsp_ADSREnvelope.h"
#include "chardsp/chardsp_Oscillator.h"
#include "chardsp/chardsp_VoicePool.h"

namespace
{
struct Voice
{
    void prepare (float rate, size_t) noexcept
    {
        oscillator.prepare (rate);
        envelope.prepare (rate);
        envelope.setAttack (.005f);
        envelope.setDecay (.05f);
        envelope.setSustain (.8f);
    }
    void start (const int &note, float nextVelocity) noexcept
    {
        oscillator.setFrequency (440.0f * std::exp2 ((static_cast<float> (note) - 69.0f) / 12.0f));
        oscillator.reset ();
        velocity = std::clamp (nextVelocity, 0.0f, 1.0f);
        envelope.trigger ();
    }
    void release () noexcept
    {
        envelope.release ();
    }
    void forceRelease (float seconds) noexcept
    {
        envelope.forceRelease (seconds);
    }
    bool isAvailable () const noexcept
    {
        return !envelope.isActive ();
    }
    void setRelease (float seconds) noexcept
    {
        envelope.setRelease (seconds);
    }
    float next () noexcept
    {
        return envelope.isActive () ? oscillator.next () * envelope.next () * velocity : 0.0f;
    }
    chardsp::SineOscillator<float> oscillator;
    chardsp::ADSREnvelope<float> envelope;
    float velocity = 0;
};
template <size_t count> struct Pool
{
    static constexpr size_t voiceCount = count;
    void prepare (float rate)
    {
        pool.prepare (rate);
    }
    void start (int note, float velocity)
    {
        pool.start (note, velocity);
    }
    void stop (int note)
    {
        pool.stop (note);
    }
    void setSustain (bool on)
    {
        pool.setSustain (on);
    }
    void releaseAll ()
    {
        pool.releaseAll ();
    }
    void setStealFadeTime (float time)
    {
        pool.setStealFadeTime (time);
    }
    float next ()
    {
        return pool.next ();
    }
    Voice &getVoice (size_t index)
    {
        return pool.getVoice (index);
    }
    chardsp::VoicePool<Voice, count, int> pool;
};

class VoicePoolDemo final : public chardsp::examples::Demo
{
public:
    std::string_view getName () const noexcept override
    {
        return "Voice pool";
    }
    std::string_view getPageConfig () const noexcept override
    {
        return R"JS({title:"Voice pool",description:"Hold more sine notes than the limit. Releasing voices are stolen before held voices; replacement starts after the short steal fade.",controls:[{name:"voices",label:"Voice limit",type:"number",min:1,max:4,step:1,value:2,section:"Allocation"},{name:"stealFade",label:"Steal fade time",min:0,max:.01,step:.0001,value:.002,unit:" ms",multiplier:1000,precision:1},{name:"release",label:"Normal release time",min:0,max:3,step:.01,value:.6,unit:" s",precision:2,section:"Voice envelope"},{name:"velocity",label:"Note-on velocity",min:0,max:1,step:.01,value:.8,precision:2}],actions:[{label:"Off",action:"sustain",value:0,group:"sustain",groupLabel:"Sustain",selected:true},{label:"On",action:"sustain",value:1,group:"sustain"},{label:"Release all notes",action:"releaseAll",clearNotes:true}],notes:{on:"noteOn",off:"noteOff",root:60,count:25}})JS";
    }
    void sampleRateChanged (double rate) override
    {
        sampleRate = static_cast<float> (rate);
        preparePool ();
    }
    void setControl (std::string_view name, float value) noexcept override
    {
        if (name == "voices")
            voices.store (std::clamp (value, 1.0f, 4.0f));
        else if (name == "stealFade")
            stealFade.store (std::clamp (value, 0.0f, .01f));
        else if (name == "release")
            releaseTime.store (std::clamp (value, 0.0f, 3.0f));
        else if (name == "velocity")
            velocity.store (std::clamp (value, 0.0f, 1.0f));
    }
    bool performAction (std::string_view name, int value) noexcept override
    {
        if (name == "noteOn")
            return actions.push ({0, value});
        if (name == "noteOff")
            return actions.push ({1, value});
        if (name == "sustain")
            return actions.push ({2, value});
        if (name == "releaseAll")
            return actions.push ({3, 0});
        return false;
    }
    void processSubBlock (const choc::audio::AudioMIDIBlockDispatcher::Block &block,
                          bool replaceOutput) override
    {
        if (currentCount () != activeCount)
            preparePool ();
        withPool ([this] (auto &pool) {
            pool.setStealFadeTime (stealFade.load ());
            for (size_t i = 0; i < pool.voiceCount; ++i)
                pool.getVoice (i).setRelease (releaseTime.load ());
        });

        for (const auto &midi : block.midiMessages)
        {
            const auto message = midi.message;

            if (message.length () < 3)
                continue;

            withPool ([&] (auto &pool) {
                if (message.isNoteOn ())
                    pool.start (static_cast<int> (message.getNoteNumber ()),
                                static_cast<float> (message.getVelocity ()) / 127.0f);
                else if (message.isNoteOff ())
                    pool.stop (static_cast<int> (message.getNoteNumber ()));
                else if (message.isAllNotesOff ())
                    pool.releaseAll ();
            });
        }

        Action action{};
        while (actions.pop (action))
            withPool ([&] (auto &pool) {
                if (action.type == 0)
                    pool.start (action.value, velocity.load ());
                else if (action.type == 1)
                    pool.stop (action.value);
                else if (action.type == 2)
                    pool.setSustain (action.value != 0);
                else
                    pool.releaseAll ();
            });
        chardsp::examples::renderMono (block, replaceOutput, [this] {
            float sample = 0;
            withPool ([&] (auto &pool) { sample = pool.next (); });
            return sample * .15f;
        });
    }

private:
    using Pools = std::variant<Pool<1>, Pool<2>, Pool<3>, Pool<4>>;
    struct Action
    {
        int type;
        int value;
    };
    template <typename Function> void withPool (Function &&function)
    {
        std::visit ([&] (auto &pool) { function (pool); }, pools);
    }
    size_t currentCount () const
    {
        return static_cast<size_t> (
            std::clamp (static_cast<int> (std::lround (voices.load ())), 1, 4));
    }
    void preparePool ()
    {
        const auto count = currentCount ();
        if (count == 1)
            pools.emplace<0> ();
        else if (count == 2)
            pools.emplace<1> ();
        else if (count == 3)
            pools.emplace<2> ();
        else
            pools.emplace<3> ();
        activeCount = count;
        withPool ([this] (auto &pool) { pool.prepare (sampleRate); });
    }
    chardsp::examples::ActionQueue<Action> actions;
    std::atomic<float> voices{2}, stealFade{.002f}, releaseTime{.6f}, velocity{.8f};
    float sampleRate = 48000;
    size_t activeCount = 2;
    Pools pools{std::in_place_index<1>};
};
} // namespace
int main ()
{
    VoicePoolDemo demo;
    return chardsp::examples::runDemo (demo);
}
