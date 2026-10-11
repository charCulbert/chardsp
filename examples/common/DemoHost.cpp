#include "DemoHost.h"

#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <utility>

#include "choc/audio/io/choc_RtAudioPlayer.h"
#include "choc/gui/choc_DesktopWindow.h"
#include "choc/gui/choc_MessageLoop.h"
#include "choc/gui/choc_WebView.h"

namespace chardsp::examples
{
namespace
{
bool hasOutputDevice ()
{
    try
    {
        RtAudio audio;
        for (const auto deviceID : audio.getDeviceIds ())
            if (audio.getDeviceInfo (deviceID).outputChannels > 0)
                return true;
    }
    catch (const std::exception &)
    {
    }

    return false;
}

std::string makeHTML (Demo &demo, const std::string &audioStatus)
{
    return std::string (R"PAGE(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
:root{color-scheme:dark}body{margin:0;background:#111;color:#ddd;font:14px -apple-system,BlinkMacSystemFont,sans-serif}main{max-width:680px;margin:auto;padding:12px}section{background:#181818;border:1px solid #333;padding:12px}h1,p{margin-top:0}.status{color:#999}.actions{display:flex;flex-wrap:wrap;gap:7px;margin:12px 0}.choice,.toggle{display:flex;align-items:center;gap:6px}.choice select{padding:6px}.action{padding:7px 9px;border:1px solid #555;background:#222;color:#ddd;cursor:pointer}.controls{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px 12px}.control-heading{grid-column:1/-1;margin:5px 0 -1px;color:#aaa;font-size:13px}.control{display:grid;grid-template-columns:120px 1fr 58px;gap:6px;align-items:center}.control input{width:100%}.control output{color:#999;text-align:right}compost-piano{--compost-piano-height:122px;--compost-piano-line:#555;--compost-piano-accent:#6687c5}compost-piano[hidden]{display:none}@media(max-width:540px){.controls{grid-template-columns:1fr}}
</style></head><body><main><section><h1 id="title"></h1><p id="description"></p><p class="status">)PAGE") +
           audioStatus + R"PAGE(</p><p class="status" id="error" hidden></p>
<div class="actions" id="actions"></div><div class="controls" id="controls"></div>
</section></main><compost-piano id="piano" dock hidden></compost-piano>
<script type="module" src="/compost-piano.js"></script><script>
const demo=)PAGE" +
           std::string (demo.getPageConfig ()) + R"PAGE(;
const selected={};
const error=document.querySelector('#error');
function showError(text){error.textContent=text;error.hidden=false}
function sendAction(name,value=0){const result=window.chardsp_action?.(name,value);if(!result){showError('C++ action binding unavailable.');return}result.then?.(ok=>{if(!ok)showError('Action queue is full; try again.')}).catch(()=>showError('C++ action binding unavailable.'))}
function sendControl(input){const value=Number(input.value);const output=input.parentElement.querySelector('output');if(output)output.textContent=(value*(Number(input.dataset.multiplier)||1)).toFixed(Number(input.dataset.precision)||0)+(input.dataset.unit||'');window.chardsp_setControl?.(input.dataset.control,value)}
function isVisible(item){if(!item.when)return true;return item.when.values.includes(Number(selected[item.when.group]))}
function updateVisibility(){document.querySelectorAll('[data-when-group]').forEach(element=>{const values=element.dataset.whenValues.split(',').map(Number);element.hidden=!values.includes(Number(selected[element.dataset.whenGroup]))})}
function markCondition(element,item){if(!item.when)return;element.dataset.whenGroup=item.when.group;element.dataset.whenValues=item.when.values.join(',')}
function renderActions(){const parent=document.querySelector('#actions');const groups=new Set;for(const item of demo.actions||[]){if(item.group){if(groups.has(item.group))continue;groups.add(item.group);const choices=demo.actions.filter(choice=>choice.group===item.group);const label=document.createElement('label');label.className='choice';label.append(document.createTextNode((item.groupLabel||item.group)+':'));const select=document.createElement('select');choices.forEach((choice,index)=>{const option=document.createElement('option');option.value=index;option.textContent=choice.label;option.selected=Boolean(choice.selected);select.append(option);if(choice.selected)selected[item.group]=choice.value??0});if(selected[item.group]===undefined)selected[item.group]=choices[0].value??0;select.addEventListener('change',()=>{const choice=choices[Number(select.value)];selected[item.group]=choice.value??0;sendAction(choice.action,choice.value??0);updateVisibility()});label.append(select);markCondition(label,item);parent.append(label)}else{const button=document.createElement('button');button.className='action';button.textContent=item.label;button.addEventListener('click',()=>{if(item.clearNotes)piano.allNotesOff?.();sendAction(item.action,item.value??0)});markCondition(button,item);parent.append(button)}}}
function renderControls(){const parent=document.querySelector('#controls');let section='';for(const item of demo.controls||[]){if(item.section&&item.section!==section){section=item.section;const heading=document.createElement('h2');heading.className='control-heading';heading.textContent=section;parent.append(heading)}const row=document.createElement('label');row.className='control';const label=document.createElement('span');label.textContent=item.label;const input=document.createElement('input');input.type=item.type||'range';input.min=item.min;input.max=item.max;input.step=item.step;input.value=item.value;input.dataset.control=item.name;input.dataset.unit=item.unit||'';input.dataset.multiplier=item.multiplier||1;input.dataset.precision=item.precision||0;const output=document.createElement('output');row.append(label,input,output);markCondition(row,item);parent.append(row);input.addEventListener('input',()=>sendControl(input));sendControl(input)}}
const piano=document.querySelector('#piano');if(demo.notes){piano.hidden=false;piano.setAttribute('root-note',demo.notes.root||60);piano.setAttribute('note-count',demo.notes.count||25);piano.addEventListener('note-down',event=>sendAction(demo.notes.on,event.detail.note));piano.addEventListener('note-up',event=>sendAction(demo.notes.off,event.detail.note))}
document.querySelector('#title').textContent=demo.title;document.querySelector('#description').textContent=demo.description;renderActions();renderControls();updateVisibility();
</script></body></html>)PAGE";
}

class DemoWindow
{
public:
    DemoWindow (Demo &nextDemo, std::string nextAudioStatus)
        : demo (nextDemo), audioStatus (std::move (nextAudioStatus)), window ({100, 100, 720, 620})
    {
        window.setWindowTitle (std::string (demo.getName ()));
        window.centreWithSize (720, 620);
        window.setResizable (true);
        window.setMinimumSize (480, 400);
        window.windowClosed = [] { choc::messageloop::stop (); };

        choc::ui::WebView::Options options;
        options.enableDebugMode = true;
        options.enableDebugInspector = true;
        options.acceptsFirstMouseClick = true;
        options.fetchResource =
            [this] (
                const std::string &path) -> std::optional<choc::ui::WebView::Options::Resource> {
            if (path == "/")
                return choc::ui::WebView::Options::Resource (makeHTML (demo, audioStatus),
                                                             "text/html");

            if (path != "/compost-piano.js" && path != "/midi.js" && path != "/utils.js")
                return std::nullopt;

            std::ifstream stream (std::string (CHARDSP_DEMO_ASSET_DIR) + path, std::ios::binary);
            if (!stream)
                return std::nullopt;

            return choc::ui::WebView::Options::Resource (
                std::string ((std::istreambuf_iterator<char> (stream)),
                             std::istreambuf_iterator<char> ()),
                "text/javascript");
        };
        options.webviewIsReady = [this] (choc::ui::WebView &view) {
            view.bind ("chardsp_setControl", [this] (const choc::value::ValueView &args) {
                if (args.isArray () && args.size () >= 2)
                    demo.setControl (args[0].getWithDefault<std::string> (""),
                                     static_cast<float> (args[1].getWithDefault<double> (0.0)));
                return choc::value::Value{};
            });
            view.bind ("chardsp_action", [this] (const choc::value::ValueView &args) {
                if (!args.isArray () || args.size () == 0)
                    return choc::value::Value (false);
                return choc::value::Value (
                    demo.performAction (args[0].getWithDefault<std::string> (""),
                                        args.size () > 1 ? args[1].getWithDefault<int> (0) : 0));
            });
            view.navigate ({});
        };

        webView = std::make_unique<choc::ui::WebView> (options);
        window.setContent (webView->getViewHandle ());
    }

    void run ()
    {
        window.setVisible (true);
        window.toFront ();
        choc::messageloop::run ();
    }

private:
    Demo &demo;
    std::string audioStatus;
    choc::ui::DesktopWindow window;
    std::unique_ptr<choc::ui::WebView> webView;
};
} // namespace

int runDemo (Demo &demo)
{
    choc::ui::setWindowsDPIAwareness ();
    choc::messageloop::initialise ();

    std::unique_ptr<choc::audio::io::RtAudioMIDIPlayer> player;
    std::string audioStatus = "Audio unavailable; controls remain usable.";

    if (hasOutputDevice ())
    {
        choc::audio::io::AudioDeviceOptions options;
        options.sampleRate = 48000;
        options.blockSize = 256;
        options.outputChannelCount = 2;
        options.inputChannelCount = 0;
        player = std::make_unique<choc::audio::io::RtAudioMIDIPlayer> (
            options,
            [] (const std::string &message) { std::cout << "[audio] " << message << '\n'; });

        if (const auto error = player->getLastError (); error.empty ())
        {
            player->addCallback (demo);
            audioStatus = "Audio active.";
        }
        else
        {
            std::cerr << "Could not open audio: " << error << '\n';
            player.reset ();
        }
    }

    DemoWindow window (demo, std::move (audioStatus));
    window.run ();

    if (player != nullptr)
        player->removeCallback (demo);

    return 0;
}
} // namespace chardsp::examples
