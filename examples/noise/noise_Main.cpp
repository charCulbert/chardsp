#include "../common/DemoHost.h"

#include <algorithm>
#include <atomic>

#include "chardsp/chardsp_ColoredNoise.h"

namespace
{
class NoiseDemo final : public chardsp::examples::Demo
{
public:
    std::string_view getName () const noexcept override
    {
        return "Colored noise";
    }
    std::string_view getPageConfig () const noexcept override
    {
        return R"JS({title:"Colored noise",description:"Switch between brown, pink, white, and blue noise from one generator.",controls:[{name:"level",label:"Output level",min:0,max:.3,step:.01,value:.12,precision:2}],actions:[{label:"Brown",action:"color",value:0,group:"color",groupLabel:"Colour",selected:true},{label:"Pink",action:"color",value:1,group:"color"},{label:"White",action:"color",value:2,group:"color"},{label:"Blue",action:"color",value:3,group:"color"},{label:"Reset same seed",action:"reset"},{label:"Off",action:"enabled",value:0,group:"enabled",groupLabel:"Output"},{label:"On",action:"enabled",value:1,group:"enabled",selected:true}]})JS";
    }
    void sampleRateChanged (double) override
    {
        noise.reset (seed);
        noise.setColor (chardsp::NoiseColor::brown);
    }
    void setControl (std::string_view name, float value) noexcept override
    {
        if (name == "level")
            level.store (std::clamp (value, 0.0f, 0.3f));
    }
    bool performAction (std::string_view name, int value) noexcept override
    {
        if (name == "color")
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
                noise.setColor (static_cast<chardsp::NoiseColor> (std::clamp (action.value, 0, 3)));
            else if (action.type == 1)
                enabled = action.value != 0;
            else
                noise.reset (seed);
        }
        chardsp::examples::renderMono (block, replaceOutput, [this] {
            return enabled ? noise.next () * level.load () : 0.0f;
        });
    }

private:
    static constexpr uint32_t seed = 0x12345678u;
    struct Action
    {
        int type;
        int value;
    };
    chardsp::examples::ActionQueue<Action> actions;
    std::atomic<float> level{.12f};
    bool enabled = true;
    chardsp::ColoredNoise<float> noise;
};
} // namespace
int main ()
{
    NoiseDemo demo;
    return chardsp::examples::runDemo (demo);
}
