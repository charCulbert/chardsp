#include "../common/DemoHost.h"

#include <algorithm>
#include <atomic>
#include <cmath>

#include "chardsp/chardsp_Oscillator.h"

namespace
{
class OscillatorDemo final : public chardsp::examples::Demo
{
public:
    std::string_view getName () const noexcept override
    {
        return "Oscillators";
    }
    std::string_view getPageConfig () const noexcept override
    {
        return R"JS({title:"Oscillators",description:"Compare naive, PolyBLEP, elliptic BLEP, and sine oscillators. Pulse width appears only for pulse oscillators.",controls:[{name:"frequency",label:"Frequency",min:40,max:12000,step:1,value:220,unit:" Hz"},{name:"pulseWidth",label:"Pulse width",min:.02,max:.98,step:.01,value:.35,precision:2,when:{group:"oscillator",values:[1,5,8]}}],actions:[{label:"Naive saw",action:"oscillator",value:0,group:"oscillator",groupLabel:"Oscillator",selected:true},{label:"Naive pulse",action:"oscillator",value:1,group:"oscillator"},{label:"Naive triangle",action:"oscillator",value:2,group:"oscillator"},{label:"Sine",action:"oscillator",value:3,group:"oscillator"},{label:"PolyBLEP saw",action:"oscillator",value:4,group:"oscillator"},{label:"PolyBLEP pulse",action:"oscillator",value:5,group:"oscillator"},{label:"PolyBLEP triangle",action:"oscillator",value:6,group:"oscillator"},{label:"Elliptic saw",action:"oscillator",value:7,group:"oscillator"},{label:"Elliptic pulse",action:"oscillator",value:8,group:"oscillator"},{label:"Elliptic triangle",action:"oscillator",value:9,group:"oscillator"},{label:"Descending",action:"direction",value:0,group:"direction",groupLabel:"Saw direction",selected:true,when:{group:"oscillator",values:[0,4,7]}},{label:"Ascending",action:"direction",value:1,group:"direction",when:{group:"oscillator",values:[0,4,7]}},{label:"Reset phase",action:"reset"},{label:"Off",action:"enabled",value:0,group:"enabled",groupLabel:"Output"},{label:"On",action:"enabled",value:1,group:"enabled",selected:true}]})JS";
    }

    void sampleRateChanged (double rate) override
    {
        sampleRate = static_cast<float> (rate);
        prepareAll ();
    }

    void setControl (std::string_view name, float value) noexcept override
    {
        if (name == "frequency")
            frequency.store (std::clamp (value, 40.0f, 12000.0f));
        if (name == "pulseWidth")
            pulseWidth.store (std::clamp (value, 0.02f, 0.98f));
    }

    bool performAction (std::string_view name, int value) noexcept override
    {
        if (name == "oscillator")
            return actions.push ({Type::oscillator, value});
        if (name == "direction")
            return actions.push ({Type::direction, value});
        if (name == "enabled")
            return actions.push ({Type::enabled, value});
        if (name == "reset")
            return actions.push ({Type::reset, 0});
        return false;
    }

    void processSubBlock (const choc::audio::AudioMIDIBlockDispatcher::Block &block,
                          bool replace) override
    {
        applyActions ();
        setParameters ();
        chardsp::examples::renderMono (block, replace,
                                       [this] { return enabled ? next () * 0.16f : 0.0f; });
    }

private:
    enum class Type
    {
        oscillator,
        direction,
        enabled,
        reset
    };
    struct Action
    {
        Type type;
        int value;
    };
    chardsp::examples::ActionQueue<Action> actions;
    std::atomic<float> frequency{220.0f}, pulseWidth{0.35f};
    float sampleRate = 48000.0f;
    int selected = 0;
    bool enabled = true;
    chardsp::NaiveSawOscillator<float> naiveSaw;
    chardsp::NaivePulseOscillator<float> naivePulse;
    chardsp::NaiveTriangleOscillator<float> naiveTriangle;
    chardsp::SineOscillator<float> sine;
    chardsp::PolyBLEPSawOscillator<float> polySaw;
    chardsp::PolyBLEPPulseOscillator<float> polyPulse;
    chardsp::PolyBLEPTriangleOscillator<float> polyTriangle;
    chardsp::EllipticSawOscillator<float> ellipticSaw;
    chardsp::EllipticPulseOscillator<float> ellipticPulse;
    chardsp::EllipticTriangleOscillator<float> ellipticTriangle;

    template <typename Function> void each (Function &&function)
    {
        function (naiveSaw);
        function (naivePulse);
        function (naiveTriangle);
        function (sine);
        function (polySaw);
        function (polyPulse);
        function (polyTriangle);
        function (ellipticSaw);
        function (ellipticPulse);
        function (ellipticTriangle);
    }

    void prepareAll ()
    {
        each ([this] (auto &oscillator) { oscillator.prepare (sampleRate); });
        resetAll ();
    }

    void resetAll ()
    {
        each ([this] (auto &oscillator) { oscillator.reset (); });
    }

    void setParameters ()
    {
        const auto hz = frequency.load ();
        each ([hz] (auto &oscillator) { oscillator.setFrequency (hz); });
        const auto width = pulseWidth.load ();
        naivePulse.setPulseWidth (width);
        polyPulse.setPulseWidth (width);
        ellipticPulse.setPulseWidth (width);
    }

    void applyActions ()
    {
        Action action{};
        while (actions.pop (action))
        {
            if (action.type == Type::oscillator)
            {
                selected = std::clamp (action.value, 0, 9);
                resetAll ();
            }
            else if (action.type == Type::enabled)
                enabled = action.value != 0;
            else if (action.type == Type::reset)
                resetAll ();
            else
            {
                const auto direction = action.value == 0 ? chardsp::SawDirection::descending
                                                         : chardsp::SawDirection::ascending;
                naiveSaw.setDirection (direction);
                polySaw.setDirection (direction);
                ellipticSaw.setDirection (direction);
            }
        }
    }

    float next ()
    {
        switch (selected)
        {
        case 0:
            return naiveSaw.next ();
        case 1:
            return naivePulse.next ();
        case 2:
            return naiveTriangle.next ();
        case 3:
            return sine.next ();
        case 4:
            return polySaw.next ();
        case 5:
            return polyPulse.next ();
        case 6:
            return polyTriangle.next ();
        case 7:
            return ellipticSaw.next ();
        case 8:
            return ellipticPulse.next ();
        case 9:
            return ellipticTriangle.next ();
        }
        return 0.0f;
    }
};
} // namespace

int main ()
{
    OscillatorDemo demo;
    return chardsp::examples::runDemo (demo);
}
