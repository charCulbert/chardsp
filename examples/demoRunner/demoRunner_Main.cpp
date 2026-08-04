#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "chardsp/chardsp_ADSREnvelope.h"
#include "chardsp/chardsp_BasicSynthVoice.h"
#include "chardsp/chardsp_ColoredNoise.h"
#include "chardsp/chardsp_DelayLine.h"
#include "chardsp/chardsp_Downsampler4x.h"
#include "chardsp/chardsp_MonoVoiceController.h"
#include "chardsp/chardsp_Oscillator.h"
#include "chardsp/chardsp_Oversampler.h"
#include "chardsp/chardsp_Portamento.h"
#include "chardsp/chardsp_StateVariableFilter.h"
#include "chardsp/chardsp_VoicePool.h"
#include "choc/audio/io/choc_RtAudioPlayer.h"
#include "choc/gui/choc_DesktopWindow.h"
#include "choc/gui/choc_MessageLoop.h"
#include "choc/gui/choc_WebView.h"

namespace
{
enum class Demo : uint8_t
{
    oscillators = 1,
    adsr,
    filter,
    noise,
    downsampler,
    delay,
    oversampler,
    voicePool,
    monoPortamento,
    basicSynth
};

constexpr uint32_t maxBlockSize = 2048;

struct PoolVoice
{
    void prepare (float newSampleRate, size_t) noexcept
    {
        oscillator.prepare (newSampleRate);
        oscillator.reset();
        envelope.prepare (newSampleRate);
        envelope.setShape (chardsp::EnvelopeShape::exponential);
        envelope.setAttack (0.005f);
        envelope.setDecay (0.05f);
        envelope.setSustain (0.8f);
        envelope.setRelease (0.25f);
        velocity = 0.0f;
    }

    void start (const int& note, float nextVelocity) noexcept
    {
        oscillator.setFrequency (440.0f * std::exp2 (
            (static_cast<float> (note) - 69.0f) / 12.0f));
        oscillator.reset();
        velocity = std::clamp (nextVelocity, 0.0f, 1.0f);
        envelope.trigger();
    }

    void release() noexcept { envelope.release(); }
    void forceRelease (float seconds) noexcept { envelope.forceRelease (seconds); }
    bool isAvailable() const noexcept { return ! envelope.isActive(); }
    void setRelease (float seconds) noexcept { envelope.setRelease (seconds); }

    float next() noexcept
    {
        if (! envelope.isActive())
            return 0.0f;

        return oscillator.next() * envelope.next() * velocity;
    }

private:
    float velocity = 0.0f;
    chardsp::SineOscillator<float> oscillator;
    chardsp::ADSREnvelope<float> envelope;
};

struct SynthPoolVoice
{
    void prepare (float sampleRate, size_t voiceIndex) noexcept
    {
        voice.prepare (sampleRate, voiceIndex);
    }

    void start (const int& note, float velocity) noexcept
    {
        voice.start (static_cast<float> (note), velocity);
    }

    void release() noexcept { voice.release(); }
    void forceRelease (float seconds) noexcept { voice.forceRelease (seconds); }
    bool isAvailable() const noexcept { return voice.isAvailable(); }
    float next() noexcept { return voice.next(); }

    void setOscillator1Level (float value) noexcept
    {
        voice.setOscillator1Level (value);
    }

    void setOscillator1Waveform (chardsp::BasicSynthWaveform waveform) noexcept
    {
        voice.setOscillator1Waveform (waveform);
    }

    void setOscillator2Waveform (chardsp::BasicSynthWaveform waveform) noexcept
    {
        voice.setOscillator2Waveform (waveform);
    }

    void setOscillator1PulseWidth (float value) noexcept
    {
        voice.setOscillator1PulseWidth (value);
    }

    void setOscillator2PulseWidth (float value) noexcept
    {
        voice.setOscillator2PulseWidth (value);
    }

    void setOscillator2Level (float value) noexcept
    {
        voice.setOscillator2Level (value);
    }

    void setNoiseLevel (float value) noexcept { voice.setNoiseLevel (value); }
    void setNoiseColor (chardsp::NoiseColor color) noexcept
    {
        voice.setNoiseColor (color);
    }

    void setEnvelopeShape (chardsp::EnvelopeShape shape) noexcept
    {
        voice.setEnvelopeShape (shape);
    }

    void setAttack (float value) noexcept { voice.setAttack (value); }
    void setDecay (float value) noexcept { voice.setDecay (value); }
    void setSustain (float value) noexcept { voice.setSustain (value); }
    void setRelease (float value) noexcept { voice.setRelease (value); }
    void setFilter (chardsp::FilterMode mode, float cutoff, float q) noexcept
    {
        voice.setFilter (mode, cutoff, q);
    }

    void setVelocitySensitivity (float value) noexcept
    {
        voice.setVelocitySensitivity (value);
    }

    void setVelocityCurve (float value) noexcept
    {
        voice.setVelocityCurve (value);
    }

    void configureDefaults() noexcept
    {
        setOscillator1Waveform (chardsp::BasicSynthWaveform::saw);
        setOscillator2Waveform (chardsp::BasicSynthWaveform::sine);
        setOscillator1Level (0.75f);
        setOscillator2Level (0.25f);
        setNoiseColor (chardsp::NoiseColor::white);
        setNoiseLevel (0.02f);
        setAttack (0.01f);
        setDecay (0.18f);
        setSustain (0.62f);
        setRelease (0.6f);
        setFilter (chardsp::FilterMode::lowpass, 4000.0f, 0.8f);
        setVelocitySensitivity (1.0f);
        setVelocityCurve (1.0f);
    }

private:
    chardsp::BasicSynthVoice<float> voice;
};

struct SynthVoicePool
{
    static constexpr size_t voiceCount = 8;

    void prepare (float sampleRate) noexcept { pool.prepare (sampleRate); }
    void setStealFadeTime (float seconds) noexcept
    {
        pool.setStealFadeTime (seconds);
    }
    void start (int note, float velocity) noexcept { pool.start (note, velocity); }
    void stop (int note) noexcept { pool.stop (note); }
    void releaseAll() noexcept { pool.releaseAll(); }
    void reset() noexcept { pool.reset(); }
    float next() noexcept { return pool.next(); }
    SynthPoolVoice& getVoice (size_t index) noexcept { return pool.getVoice (index); }

private:
    chardsp::VoicePool<SynthPoolVoice, voiceCount, int> pool;
};

template <size_t count>
struct DemoVoicePool
{
    static constexpr size_t voiceCount = count;

    void prepare (float sampleRate) noexcept { pool.prepare (sampleRate); }
    void setStealFadeTime (float seconds) noexcept { pool.setStealFadeTime (seconds); }
    void start (int note, float velocity) noexcept { pool.start (note, velocity); }
    void stop (int note) noexcept { pool.stop (note); }
    void setSustain (bool active) noexcept { pool.setSustain (active); }
    void releaseAll() noexcept { pool.releaseAll(); }
    float next() noexcept { return pool.next(); }
    PoolVoice& getVoice (size_t index) noexcept { return pool.getVoice (index); }

private:
    chardsp::VoicePool<PoolVoice, count, int> pool;
};

enum class DemoActionType : uint8_t
{
    setToneEnabled,
    setOscillator,
    setSawDirection,
    resetWaveform,
    triggerEnvelope,
    retriggerEnvelope,
    releaseEnvelope,
    forceReleaseEnvelope,
    setEnvelopeShape,
    setFilterMode,
    setNoiseColor,
    resetNoise,
    setDownsampleMode,
    resetDownsampler,
    triggerDelay,
    clearDelay,
    setOversampleMode,
    resetOversampler,
    voiceNoteOn,
    voiceNoteOff,
    setVoiceSustain,
    voiceReleaseAll,
    monoNoteOn,
    monoNoteOff,
    setMonoSustain,
    monoAllOff,
    setMonoLegato,
    setMonoPortamentoMode,
    setSynthOscillator1Waveform,
    setSynthOscillator2Waveform,
    setSynthNoiseColor,
    setSynthEnvelopeShape,
    setSynthFilterMode,
    synthNoteOn,
    synthNoteOff,
    synthReleaseAll
};

struct DemoAction
{
    DemoActionType type = DemoActionType::setToneEnabled;
    int value = 0;
};

class DemoCallback final : public choc::audio::io::AudioMIDICallback
{
public:
    void selectDemo (Demo demo) noexcept
    {
        requestedDemo.store (demo, std::memory_order_relaxed);
    }

    bool queueAction (const std::string& name, int value) noexcept
    {
        DemoActionType type;

        if (name == "setToneEnabled")             type = DemoActionType::setToneEnabled;
        else if (name == "setOscillator")         type = DemoActionType::setOscillator;
        else if (name == "setSawDirection")       type = DemoActionType::setSawDirection;
        else if (name == "resetWaveform")        type = DemoActionType::resetWaveform;
        else if (name == "triggerEnvelope")      type = DemoActionType::triggerEnvelope;
        else if (name == "retriggerEnvelope")    type = DemoActionType::retriggerEnvelope;
        else if (name == "releaseEnvelope")      type = DemoActionType::releaseEnvelope;
        else if (name == "forceReleaseEnvelope") type = DemoActionType::forceReleaseEnvelope;
        else if (name == "setEnvelopeShape")     type = DemoActionType::setEnvelopeShape;
        else if (name == "setFilterMode")        type = DemoActionType::setFilterMode;
        else if (name == "setNoiseColor")        type = DemoActionType::setNoiseColor;
        else if (name == "resetNoise")           type = DemoActionType::resetNoise;
        else if (name == "setDownsampleMode")    type = DemoActionType::setDownsampleMode;
        else if (name == "resetDownsampler")     type = DemoActionType::resetDownsampler;
        else if (name == "triggerDelay")         type = DemoActionType::triggerDelay;
        else if (name == "clearDelay")           type = DemoActionType::clearDelay;
        else if (name == "setOversampleMode")    type = DemoActionType::setOversampleMode;
        else if (name == "resetOversampler")     type = DemoActionType::resetOversampler;
        else if (name == "voiceNoteOn")          type = DemoActionType::voiceNoteOn;
        else if (name == "voiceNoteOff")         type = DemoActionType::voiceNoteOff;
        else if (name == "setVoiceSustain")      type = DemoActionType::setVoiceSustain;
        else if (name == "voiceReleaseAll")      type = DemoActionType::voiceReleaseAll;
        else if (name == "monoNoteOn")           type = DemoActionType::monoNoteOn;
        else if (name == "monoNoteOff")          type = DemoActionType::monoNoteOff;
        else if (name == "setMonoSustain")       type = DemoActionType::setMonoSustain;
        else if (name == "monoAllOff")           type = DemoActionType::monoAllOff;
        else if (name == "setMonoLegato")        type = DemoActionType::setMonoLegato;
        else if (name == "setMonoPortamentoMode") type = DemoActionType::setMonoPortamentoMode;
        else if (name == "setSynthOscillator1Waveform") type = DemoActionType::setSynthOscillator1Waveform;
        else if (name == "setSynthOscillator2Waveform") type = DemoActionType::setSynthOscillator2Waveform;
        else if (name == "setSynthNoiseColor") type = DemoActionType::setSynthNoiseColor;
        else if (name == "setSynthEnvelopeShape") type = DemoActionType::setSynthEnvelopeShape;
        else if (name == "setSynthFilterMode") type = DemoActionType::setSynthFilterMode;
        else if (name == "synthNoteOn")          type = DemoActionType::synthNoteOn;
        else if (name == "synthNoteOff")         type = DemoActionType::synthNoteOff;
        else if (name == "synthReleaseAll")      type = DemoActionType::synthReleaseAll;
        else                                      return false;

        return enqueueAction ({ type, value });
    }

    void setControl (const std::string& name, float value) noexcept
    {
        if (! std::isfinite (value))
            return;

        if (name == "frequency")
            frequencyControl.store (std::clamp (value, 40.0f, 12000.0f),
                                    std::memory_order_relaxed);
        else if (name == "samplingFrequency")
            samplingFrequencyControl.store (
                std::clamp (value, 100.0f, 12000.0f),
                std::memory_order_relaxed);
        else if (name == "drive")
            driveControl.store (std::clamp (value, 1.0f, 20.0f),
                                std::memory_order_relaxed);
        else if (name == "shape")
            pulseWidthControl.store (std::clamp (value, 0.02f, 0.98f),
                                      std::memory_order_relaxed);
        else if (name == "cutoff")
            cutoffControl.store (std::clamp (value, 20.0f, 20000.0f),
                                 std::memory_order_relaxed);
        else if (name == "q")
            qControl.store (std::clamp (value, 0.1f, 10.0f),
                            std::memory_order_relaxed);
        else if (name == "feedback")
            feedbackControl.store (std::clamp (value, 0.0f, 0.9f),
                                   std::memory_order_relaxed);
        else if (name == "attack")
            attackControl.store (std::clamp (value, 0.0f, 2.0f),
                                 std::memory_order_relaxed);
        else if (name == "decay")
            decayControl.store (std::clamp (value, 0.0f, 2.0f),
                                std::memory_order_relaxed);
        else if (name == "sustain")
            sustainControl.store (std::clamp (value, 0.0f, 1.0f),
                                  std::memory_order_relaxed);
        else if (name == "release")
            releaseControl.store (std::clamp (value, 0.0f, 3.0f),
                                  std::memory_order_relaxed);
        else if (name == "noiseLevel")
            noiseLevelControl.store (std::clamp (value, 0.0f, 0.3f),
                                     std::memory_order_relaxed);
        else if (name == "delayTime")
            delayTimeControl.store (std::clamp (value, 0.0f, 0.95f),
                                    std::memory_order_relaxed);
        else if (name == "wet")
            wetControl.store (std::clamp (value, 0.0f, 1.0f),
                              std::memory_order_relaxed);
        else if (name == "burstLength")
            burstLengthControl.store (std::clamp (value, 0.01f, 0.5f),
                                      std::memory_order_relaxed);
        else if (name == "velocity")
            velocityControl.store (std::clamp (value, 0.0f, 1.0f),
                                   std::memory_order_relaxed);
        else if (name == "portamento")
            portamentoControl.store (std::clamp (value, 0.0f, 1.0f),
                                     std::memory_order_relaxed);
        else if (name == "portamentoShape")
            portamentoShapeControl.store (std::clamp (value, 0.0f, 1.0f),
                                          std::memory_order_relaxed);
        else if (name == "timeRateMorph")
            timeRateMorphControl.store (std::clamp (value, 0.0f, 1.0f),
                                        std::memory_order_relaxed);
        else if (name == "graceTime")
            graceTimeControl.store (std::clamp (value, 0.0f, 0.25f),
                                    std::memory_order_relaxed);
        else if (name == "stealFade")
            stealFadeControl.store (std::clamp (value, 0.0f, 0.01f),
                                    std::memory_order_relaxed);
        else if (name == "voices")
            voiceCountControl.store (std::clamp (value, 1.0f, 4.0f),
                                     std::memory_order_relaxed);
        else if (name == "osc1Level")
            osc1LevelControl.store (std::clamp (value, 0.0f, 1.0f),
                                    std::memory_order_relaxed);
        else if (name == "osc2Level")
            osc2LevelControl.store (std::clamp (value, 0.0f, 1.0f),
                                    std::memory_order_relaxed);
        else if (name == "osc1PulseWidth")
            osc1PulseWidthControl.store (std::clamp (value, 0.02f, 0.98f),
                                         std::memory_order_relaxed);
        else if (name == "osc2PulseWidth")
            osc2PulseWidthControl.store (std::clamp (value, 0.02f, 0.98f),
                                         std::memory_order_relaxed);
        else if (name == "synthNoise")
            synthNoiseControl.store (std::clamp (value, 0.0f, 0.3f),
                                     std::memory_order_relaxed);
        else if (name == "velocitySensitivity")
            velocitySensitivityControl.store (
                std::clamp (value, 0.0f, 1.0f),
                std::memory_order_relaxed);
        else if (name == "velocityCurve")
            velocityCurveControl.store (std::clamp (value, 0.1f, 4.0f),
                                        std::memory_order_relaxed);
    }

    void sampleRateChanged (double newRate) override
    {
        sampleRate = std::isfinite (newRate) && newRate > 0.0
            ? static_cast<float> (newRate) : 48000.0f;
        prepareOscillators();
        samplingLowSine.prepare (sampleRate);
        samplingHighSine.prepare (sampleRate * 4.0f);
        oversampler2x.prepare (1, maxBlockSize,
                               chardsp::OversamplingFactor::x2);
        oversampler4x.prepare (1, maxBlockSize,
                               chardsp::OversamplingFactor::x4);
        resetDemo (requestedDemo.load (std::memory_order_relaxed));
    }

    void startBlock() override {}
    void endBlock() override {}

    void processSubBlock (
        const choc::audio::AudioMIDIBlockDispatcher::Block& block,
        bool replaceOutput) override
    {
        const auto requested = requestedDemo.load (std::memory_order_relaxed);

        if (requested != activeDemo)
            resetDemo (requested);

        for (const auto& midi : block.midiMessages)
            handleMIDI (midi.message);

        applyControls();
        handleActions();

        if (activeDemo == Demo::oversampler)
        {
            processOversampler (block, replaceOutput);
            return;
        }

        const auto frames = block.audioOutput.getNumFrames();

        for (choc::buffer::FrameCount frame = 0; frame < frames; ++frame)
        {
            const auto sample = nextSample();

            for (choc::buffer::ChannelCount channel = 0;
                 channel < block.audioOutput.getNumChannels(); ++channel)
            {
                auto& destination = block.audioOutput.getSample (channel, frame);
                destination = replaceOutput ? sample : destination + sample;
            }

        }
    }

private:
    using MonoController = chardsp::MonoVoiceController<float, int, 4>;
    using VoicePool1 = DemoVoicePool<1>;
    using VoicePool2 = DemoVoicePool<2>;
    using VoicePool3 = DemoVoicePool<3>;
    using VoicePool4 = DemoVoicePool<4>;
    using VoicePools = std::variant<VoicePool1, VoicePool2, VoicePool3, VoicePool4>;

    static constexpr size_t actionQueueSize = 64;

    std::atomic<Demo> requestedDemo { Demo::basicSynth };
    Demo activeDemo = Demo::basicSynth;
    float sampleRate = 48000.0f;
    uint32_t oscillatorSegment = 0;
    int midiNote = -1;
    bool toneEnabled = true;
    bool filterBypassed = false;
    int filterOutput = 1;
    uint64_t delayBurstSamples = 0;
    int downsampleMode = 1;
    int oversampleMode = 2;
    chardsp::FilterMode filterMode = chardsp::FilterMode::lowpass;
    chardsp::FilterMode synthFilterMode = chardsp::FilterMode::lowpass;
    size_t voicePoolVoiceCount = 2;

    std::atomic<float> frequencyControl { 220.0f };
    std::atomic<float> samplingFrequencyControl { 9000.0f };
    std::atomic<float> driveControl { 3.0f };
    std::atomic<float> pulseWidthControl { 0.35f };
    std::atomic<float> cutoffControl { 4000.0f };
    std::atomic<float> qControl { 0.8f };
    std::atomic<float> feedbackControl { 0.55f };
    std::atomic<float> attackControl { 0.01f };
    std::atomic<float> decayControl { 0.18f };
    std::atomic<float> sustainControl { 0.62f };
    std::atomic<float> releaseControl { 0.6f };
    std::atomic<float> noiseLevelControl { 0.12f };
    std::atomic<float> delayTimeControl { 0.25f };
    std::atomic<float> wetControl { 0.7f };
    std::atomic<float> burstLengthControl { 0.1f };
    std::atomic<float> velocityControl { 0.8f };
    std::atomic<float> portamentoControl { 0.18f };
    std::atomic<float> portamentoShapeControl { 0.5f };
    std::atomic<float> timeRateMorphControl { 0.5f };
    std::atomic<float> graceTimeControl { 0.024f };
    std::atomic<float> stealFadeControl { 0.002f };
    std::atomic<float> voiceCountControl { 2.0f };
    std::atomic<float> osc1LevelControl { 0.7f };
    std::atomic<float> osc2LevelControl { 0.3f };
    std::atomic<float> osc1PulseWidthControl { 0.5f };
    std::atomic<float> osc2PulseWidthControl { 0.5f };
    std::atomic<float> synthNoiseControl { 0.02f };
    std::atomic<float> velocitySensitivityControl { 1.0f };
    std::atomic<float> velocityCurveControl { 1.0f };

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
    chardsp::SineOscillator<float> samplingLowSine;
    chardsp::SineOscillator<float> samplingHighSine;

    chardsp::ADSREnvelope<float> envelope;
    chardsp::StateVariableFilter<float> filter;
    chardsp::ColoredNoise<float> noise;
    chardsp::Downsampler4x<float> downsampler;
    chardsp::DelayLine<float, 48000> delay;
    chardsp::Oversampler<float> oversampler2x;
    chardsp::Oversampler<float> oversampler4x;
    SynthVoicePool synthVoices;
    VoicePools voices { std::in_place_type<VoicePool2> };
    MonoController monoController;
    chardsp::Portamento<float> portamento;
    chardsp::BasicSynthVoice<float> monoVoice;

    std::array<float, maxBlockSize> oversampleInput {};
    std::array<float, maxBlockSize> oversampleOutput {};
    std::array<DemoAction, actionQueueSize> actionQueue {};
    std::atomic<uint32_t> actionWrite { 0 };
    std::atomic<uint32_t> actionRead { 0 };

    bool enqueueAction (DemoAction action) noexcept
    {
        const auto write = actionWrite.load (std::memory_order_relaxed);
        const auto read = actionRead.load (std::memory_order_acquire);

        if (write - read >= actionQueueSize)
            return false;

        actionQueue[write % actionQueueSize] = action;
        actionWrite.store (write + 1, std::memory_order_release);
        return true;
    }

    bool dequeueAction (DemoAction& action) noexcept
    {
        const auto read = actionRead.load (std::memory_order_relaxed);
        const auto write = actionWrite.load (std::memory_order_acquire);

        if (read == write)
            return false;

        action = actionQueue[read % actionQueueSize];
        actionRead.store (read + 1, std::memory_order_release);
        return true;
    }

    void handleActions() noexcept
    {
        DemoAction action;
        while (dequeueAction (action))
            handleAction (action);
    }

    void handleAction (const DemoAction& action) noexcept
    {
        switch (action.type)
        {
            case DemoActionType::setToneEnabled:
                toneEnabled = action.value != 0;
                break;

            case DemoActionType::setOscillator:
                if (activeDemo == Demo::oscillators)
                {
                    oscillatorSegment = static_cast<uint32_t> (
                        std::clamp (action.value, 0, 9));
                    resetOscillators();
                }
                break;

            case DemoActionType::setSawDirection:
                if (activeDemo == Demo::oscillators)
                {
                    const auto direction = action.value == 0
                        ? chardsp::SawDirection::descending
                        : chardsp::SawDirection::ascending;
                    naiveSaw.setDirection (direction);
                    polySaw.setDirection (direction);
                    ellipticSaw.setDirection (direction);
                }
                break;

            case DemoActionType::resetWaveform:
                if (activeDemo == Demo::oscillators)
                    resetOscillators();
                break;

            case DemoActionType::triggerEnvelope:
                if (activeDemo == Demo::adsr)
                    envelope.trigger();
                break;

            case DemoActionType::retriggerEnvelope:
                if (activeDemo == Demo::adsr)
                    envelope.retrigger();
                break;

            case DemoActionType::releaseEnvelope:
                if (activeDemo == Demo::adsr)
                    envelope.release();
                break;

            case DemoActionType::forceReleaseEnvelope:
                if (activeDemo == Demo::adsr)
                    envelope.forceRelease (0.01f);
                break;

            case DemoActionType::setEnvelopeShape:
                if (activeDemo == Demo::adsr)
                    envelope.setShape (action.value == 0
                        ? chardsp::EnvelopeShape::exponential
                        : chardsp::EnvelopeShape::linear);
                break;

            case DemoActionType::setFilterMode:
                if (activeDemo == Demo::filter)
                {
                    filterOutput = std::clamp (action.value, 0, 4);
                    if (action.value == 0)
                    {
                        filterBypassed = true;
                        filter.reset();
                    }
                    else if (action.value <= 3)
                    {
                        setFilterMode (static_cast<chardsp::FilterMode> (
                            std::clamp (action.value - 1, 0, 2)));
                    }
                    else
                    {
                        filterBypassed = false;
                        filter.reset();
                    }
                }
                break;

            case DemoActionType::setNoiseColor:
                if (activeDemo == Demo::noise)
                    noise.setColor (static_cast<chardsp::NoiseColor> (
                        std::clamp (action.value, 0, 3)));
                break;

            case DemoActionType::resetNoise:
                if (activeDemo == Demo::noise)
                    noise.reset (0x12345678u);
                break;

            case DemoActionType::setDownsampleMode:
                if (activeDemo == Demo::downsampler)
                {
                    downsampleMode = std::clamp (action.value, 0, 1);
                    resetSamplingComparison();
                }
                break;

            case DemoActionType::resetDownsampler:
                if (activeDemo == Demo::downsampler)
                    resetSamplingComparison();
                break;

            case DemoActionType::triggerDelay:
                if (activeDemo == Demo::delay)
                {
                    delayBurstSamples = std::max<uint64_t> (
                        1, static_cast<uint64_t> (sampleRate
                            * burstLengthControl.load (
                                std::memory_order_relaxed)));
                    sine.reset();
                }
                break;

            case DemoActionType::clearDelay:
                if (activeDemo == Demo::delay)
                {
                    delayBurstSamples = 0;
                    delay.reset();
                }
                break;

            case DemoActionType::setOversampleMode:
                if (activeDemo == Demo::oversampler)
                {
                    oversampleMode = std::clamp (action.value, 0, 2);
                    resetSamplingComparison();
                }
                break;

            case DemoActionType::resetOversampler:
                if (activeDemo == Demo::oversampler)
                    resetSamplingComparison();
                break;

            case DemoActionType::voiceNoteOn:
                if (activeDemo == Demo::voicePool)
                    withVoicePool ([&] (auto& pool) {
                        pool.start (action.value, velocityControl.load (
                            std::memory_order_relaxed));
                    });
                break;

            case DemoActionType::voiceNoteOff:
                if (activeDemo == Demo::voicePool)
                    withVoicePool ([&] (auto& pool) {
                        pool.stop (action.value);
                    });
                break;

            case DemoActionType::setVoiceSustain:
                if (activeDemo == Demo::voicePool)
                    withVoicePool ([&] (auto& pool) {
                        pool.setSustain (action.value != 0);
                    });
                break;

            case DemoActionType::voiceReleaseAll:
                if (activeDemo == Demo::voicePool)
                    withVoicePool ([] (auto& pool) {
                        pool.releaseAll();
                    });
                break;

            case DemoActionType::monoNoteOn:
                if (activeDemo == Demo::monoPortamento)
                    applyMonoAction (monoController.start (
                        action.value, static_cast<float> (action.value),
                        velocityControl.load (std::memory_order_relaxed)));
                break;

            case DemoActionType::monoNoteOff:
                if (activeDemo == Demo::monoPortamento)
                    applyMonoAction (monoController.stop (action.value));
                break;

            case DemoActionType::setMonoSustain:
                if (activeDemo == Demo::monoPortamento)
                    applyMonoAction (monoController.setSustain (action.value != 0));
                break;

            case DemoActionType::monoAllOff:
                if (activeDemo == Demo::monoPortamento)
                    applyMonoAction (monoController.allKeysOff());
                break;

            case DemoActionType::setMonoLegato:
                if (activeDemo == Demo::monoPortamento)
                    monoController.setLegato (action.value != 0);
                break;

            case DemoActionType::setMonoPortamentoMode:
                if (activeDemo == Demo::monoPortamento)
                    monoController.setPortamentoMode (static_cast<chardsp::PortamentoMode> (
                        std::clamp (action.value, 0, 2)));
                break;

            case DemoActionType::setSynthOscillator1Waveform:
                if (activeDemo == Demo::basicSynth)
                    for (size_t index = 0; index < SynthVoicePool::voiceCount; ++index)
                        synthVoices.getVoice (index).setOscillator1Waveform (
                            static_cast<chardsp::BasicSynthWaveform> (
                                std::clamp (action.value, 0, 3)));
                break;

            case DemoActionType::setSynthOscillator2Waveform:
                if (activeDemo == Demo::basicSynth)
                    for (size_t index = 0; index < SynthVoicePool::voiceCount; ++index)
                        synthVoices.getVoice (index).setOscillator2Waveform (
                            static_cast<chardsp::BasicSynthWaveform> (
                                std::clamp (action.value, 0, 3)));
                break;

            case DemoActionType::setSynthNoiseColor:
                if (activeDemo == Demo::basicSynth)
                    for (size_t index = 0; index < SynthVoicePool::voiceCount; ++index)
                        synthVoices.getVoice (index).setNoiseColor (
                        static_cast<chardsp::NoiseColor> (
                            std::clamp (action.value, 0, 3)));
                break;

            case DemoActionType::setSynthEnvelopeShape:
                if (activeDemo == Demo::basicSynth)
                    for (size_t index = 0; index < SynthVoicePool::voiceCount; ++index)
                        synthVoices.getVoice (index).setEnvelopeShape (
                            action.value == 0
                                ? chardsp::EnvelopeShape::exponential
                                : chardsp::EnvelopeShape::linear);
                break;

            case DemoActionType::setSynthFilterMode:
                if (activeDemo == Demo::basicSynth)
                    synthFilterMode = static_cast<chardsp::FilterMode> (
                        std::clamp (action.value, 0, 2));
                break;

            case DemoActionType::synthNoteOn:
                if (activeDemo == Demo::basicSynth)
                {
                    synthVoices.start (action.value,
                        velocityControl.load (std::memory_order_relaxed));
                }
                break;

            case DemoActionType::synthNoteOff:
                if (activeDemo == Demo::basicSynth)
                    synthVoices.stop (action.value);
                break;

            case DemoActionType::synthReleaseAll:
                if (activeDemo == Demo::basicSynth)
                {
                    synthVoices.releaseAll();
                    midiNote = -1;
                }
                break;
        }
    }

    void setFilterMode (chardsp::FilterMode mode) noexcept
    {
        if (activeDemo != Demo::filter)
            return;

        filterBypassed = false;
        filterMode = mode;
        filter.reset();
        filter.setMode (filterMode);
    }

    template <typename Function>
    void withVoicePool (Function&& function) noexcept
    {
        std::visit ([&function] (auto& pool) {
            function (pool);
        }, voices);
    }

    void prepareOscillators() noexcept
    {
        auto prepare = [this] (auto& oscillator)
        {
            oscillator.prepare (sampleRate);
        };

        prepare (naiveSaw);
        prepare (naivePulse);
        prepare (naiveTriangle);
        prepare (sine);
        prepare (polySaw);
        prepare (polyPulse);
        prepare (polyTriangle);
        prepare (ellipticSaw);
        prepare (ellipticPulse);
        prepare (ellipticTriangle);

        resetOscillators();
    }

    void resetOscillators() noexcept
    {
        const auto frequency = frequencyControl.load (std::memory_order_relaxed);
        auto reset = [frequency] (auto& oscillator)
        {
            oscillator.setFrequency (frequency);
            oscillator.reset();
        };

        reset (naiveSaw);
        reset (naivePulse);
        reset (naiveTriangle);
        reset (sine);
        reset (polySaw);
        reset (polyPulse);
        reset (polyTriangle);
        reset (ellipticSaw);
        reset (ellipticPulse);
        reset (ellipticTriangle);

        const auto pulseWidth = pulseWidthControl.load (
            std::memory_order_relaxed);
        naivePulse.setPulseWidth (pulseWidth);
        polyPulse.setPulseWidth (pulseWidth);
        ellipticPulse.setPulseWidth (pulseWidth);
    }

    void resetSamplingComparison() noexcept
    {
        samplingLowSine.reset();
        samplingHighSine.reset();
        downsampler.reset();
        oversampler2x.reset();
        oversampler4x.reset();
    }

    void applyControls() noexcept
    {
        if (activeDemo == Demo::voicePool
            && currentVoiceCount() != voicePoolVoiceCount)
            prepareVoicePool();

        const auto frequency = frequencyControl.load (std::memory_order_relaxed);
        auto setFrequency = [frequency] (auto& oscillator)
        {
            oscillator.setFrequency (frequency);
        };

        setFrequency (naiveSaw);
        setFrequency (naivePulse);
        setFrequency (naiveTriangle);
        setFrequency (sine);
        setFrequency (polySaw);
        setFrequency (polyPulse);
        setFrequency (polyTriangle);
        setFrequency (ellipticSaw);
        setFrequency (ellipticPulse);
        setFrequency (ellipticTriangle);

        const auto samplingFrequency = samplingFrequencyControl.load (
            std::memory_order_relaxed);
        samplingLowSine.setFrequency (samplingFrequency);
        samplingHighSine.setFrequency (samplingFrequency);

        const auto pulseWidth = pulseWidthControl.load (
            std::memory_order_relaxed);
        naivePulse.setPulseWidth (pulseWidth);
        polyPulse.setPulseWidth (pulseWidth);
        ellipticPulse.setPulseWidth (pulseWidth);

        const auto cutoff = cutoffControl.load (std::memory_order_relaxed);
        const auto q = qControl.load (std::memory_order_relaxed);
        const auto release = releaseControl.load (std::memory_order_relaxed);
        filter.setCutoffAndQ (cutoff, q);

        if (activeDemo == Demo::adsr)
        {
            envelope.setAttack (attackControl.load (std::memory_order_relaxed));
            envelope.setDecay (decayControl.load (std::memory_order_relaxed));
            envelope.setSustain (sustainControl.load (std::memory_order_relaxed));
            envelope.setRelease (release);
        }

        if (activeDemo == Demo::basicSynth)
        {
            for (size_t index = 0; index < SynthVoicePool::voiceCount; ++index)
            {
                auto& voice = synthVoices.getVoice (index);
                voice.setOscillator1Level (
                    osc1LevelControl.load (std::memory_order_relaxed));
                voice.setOscillator2Level (
                    osc2LevelControl.load (std::memory_order_relaxed));
                voice.setOscillator1PulseWidth (
                    osc1PulseWidthControl.load (std::memory_order_relaxed));
                voice.setOscillator2PulseWidth (
                    osc2PulseWidthControl.load (std::memory_order_relaxed));
                voice.setNoiseLevel (
                    synthNoiseControl.load (std::memory_order_relaxed));
                voice.setAttack (attackControl.load (std::memory_order_relaxed));
                voice.setDecay (decayControl.load (std::memory_order_relaxed));
                voice.setSustain (sustainControl.load (std::memory_order_relaxed));
                voice.setRelease (release);
                voice.setFilter (synthFilterMode, cutoff, q);
                voice.setVelocitySensitivity (
                    velocitySensitivityControl.load (std::memory_order_relaxed));
                voice.setVelocityCurve (
                    velocityCurveControl.load (std::memory_order_relaxed));
            }
        }

        monoVoice.setRelease (release);
        portamento.setTime (portamentoControl.load (std::memory_order_relaxed));
        portamento.setShape (
            portamentoShapeControl.load (std::memory_order_relaxed));
        portamento.setTimeRateMorph (
            timeRateMorphControl.load (std::memory_order_relaxed));
        monoController.setLegatoGraceTime (
            graceTimeControl.load (std::memory_order_relaxed));
        delay.setDelay (delayTimeControl.load (std::memory_order_relaxed)
                        * sampleRate);

        const auto stealFade = stealFadeControl.load (std::memory_order_relaxed);
        withVoicePool ([release, stealFade] (auto& pool) {
            pool.setStealFadeTime (stealFade);
            for (size_t i = 0; i < pool.voiceCount; ++i)
                pool.getVoice (i).setRelease (release);
        });

    }

    void resetDemo (Demo demo) noexcept
    {
        activeDemo = demo;
        oscillatorSegment = 0;
        midiNote = -1;
        toneEnabled = true;
        delayBurstSamples = 0;

        switch (activeDemo)
        {
            case Demo::oscillators:
                naiveSaw.setDirection (chardsp::SawDirection::descending);
                polySaw.setDirection (chardsp::SawDirection::descending);
                ellipticSaw.setDirection (chardsp::SawDirection::descending);
                resetOscillators();
                break;

            case Demo::adsr:
                envelope.prepare (sampleRate);
                envelope.setShape (chardsp::EnvelopeShape::exponential);
                envelope.setAttack (0.15f);
                envelope.setDecay (0.25f);
                envelope.setSustain (0.45f);
                envelope.setRelease (0.55f);
                sine.prepare (sampleRate);
                sine.setFrequency (220.0f);
                sine.reset();
                break;

            case Demo::filter:
                ellipticSaw.prepare (sampleRate);
                ellipticSaw.setFrequency (110.0f);
                ellipticSaw.reset();
                filter.prepare (sampleRate);
                filter.setCutoffAndQ (1200.0f, 1.2f);
                filterMode = chardsp::FilterMode::lowpass;
                filterOutput = 1;
                filterBypassed = false;
                filter.setMode (filterMode);
                break;

            case Demo::noise:
                noise.reset (0x12345678u);
                noise.setColor (chardsp::NoiseColor::brown);
                break;

            case Demo::downsampler:
                downsampleMode = 1;
                resetSamplingComparison();
                break;

            case Demo::delay:
                sine.prepare (sampleRate);
                sine.setFrequency (220.0f);
                sine.reset();
                delay.reset();
                delay.setDelay (sampleRate * 0.25f);
                break;

            case Demo::oversampler:
                oversampleMode = 2;
                resetSamplingComparison();
                break;

            case Demo::voicePool:
                prepareVoicePool();
                break;

            case Demo::monoPortamento:
                prepareMonoVoice();
                break;

            case Demo::basicSynth:
                synthFilterMode = chardsp::FilterMode::lowpass;
                prepareSynthVoices();
                break;
        }
    }

    void configureBasicVoice (chardsp::BasicSynthVoice<float>& voice) noexcept
    {
        voice.setOscillator1Waveform (chardsp::BasicSynthWaveform::saw);
        voice.setOscillator2Waveform (chardsp::BasicSynthWaveform::sine);
        voice.setOscillator1Level (0.75f);
        voice.setOscillator2Level (0.25f);
        voice.setNoiseColor (chardsp::NoiseColor::white);
        voice.setNoiseLevel (0.02f);
        voice.setAttack (0.01f);
        voice.setDecay (0.18f);
        voice.setSustain (0.62f);
        voice.setRelease (0.6f);
        voice.setFilter (chardsp::FilterMode::lowpass, 4000.0f, 0.8f);
        voice.setVelocitySensitivity (1.0f);
        voice.setVelocityCurve (1.0f);
    }

    void prepareSynthVoices() noexcept
    {
        synthVoices.prepare (sampleRate);
        synthVoices.setStealFadeTime (0.002f);
        for (size_t index = 0; index < SynthVoicePool::voiceCount; ++index)
            synthVoices.getVoice (index).configureDefaults();
    }

    size_t currentVoiceCount() const noexcept
    {
        const auto value = static_cast<int> (std::lround (
            voiceCountControl.load (std::memory_order_relaxed)));
        return static_cast<size_t> (std::clamp (value, 1, 4));
    }

    void prepareVoicePool() noexcept
    {
        const auto count = currentVoiceCount();

        switch (count)
        {
            case 1:
                if (voices.index() != 0)
                    voices.emplace<VoicePool1>();
                break;
            case 2:
                if (voices.index() != 1)
                    voices.emplace<VoicePool2>();
                break;
            case 3:
                if (voices.index() != 2)
                    voices.emplace<VoicePool3>();
                break;
            case 4:
                if (voices.index() != 3)
                    voices.emplace<VoicePool4>();
                break;
        }

        voicePoolVoiceCount = count;
        const auto release = releaseControl.load (std::memory_order_relaxed);
        const auto stealFade = stealFadeControl.load (std::memory_order_relaxed);

        withVoicePool ([this, release, stealFade] (auto& pool) {
            pool.prepare (sampleRate);
            pool.setStealFadeTime (stealFade);
            for (size_t i = 0; i < pool.voiceCount; ++i)
                pool.getVoice (i).setRelease (release);
        });
    }

    void prepareMonoVoice() noexcept
    {
        monoVoice.prepare (sampleRate, 0);
        configureBasicVoice (monoVoice);
        monoVoice.setOscillator1Level (0.0f);
        monoVoice.setOscillator2Waveform (chardsp::BasicSynthWaveform::sine);
        monoVoice.setOscillator2Level (1.0f);
        monoVoice.setNoiseLevel (0.0f);
        monoVoice.setAttack (0.01f);
        monoVoice.setRelease (0.35f);
        monoVoice.setFilter (chardsp::FilterMode::lowpass,
                             sampleRate * 0.48f, 0.707f);
        monoController.prepare (sampleRate);
        monoController.setLegato (true);
        monoController.setPortamentoMode (chardsp::PortamentoMode::always);
        portamento.prepare (sampleRate);
        portamento.reset (57.0f);
        portamento.setTime (0.18f);
        portamento.setShape (0.5f);
        portamento.setTimeRateMorph (0.5f);
    }

    void handleMIDI (choc::midi::MessageView message) noexcept
    {
        if (activeDemo != Demo::basicSynth || message.length() < 3)
            return;

        if (message.isNoteOn())
        {
            midiNote = static_cast<int> (message.getNoteNumber());
            synthVoices.start (midiNote,
                              static_cast<float> (message.getVelocity()) / 127.0f);
        }
        else if (message.isNoteOff())
        {
            if (midiNote == static_cast<int> (message.getNoteNumber()))
                synthVoices.stop (midiNote);
        }
        else if (message.isAllNotesOff())
        {
            synthVoices.releaseAll();
            midiNote = -1;
        }
        else if (message.isAllSoundOff())
        {
            synthVoices.reset();
            midiNote = -1;
        }
    }

    float nextSample() noexcept
    {
        switch (activeDemo)
        {
            case Demo::oscillators:    return toneEnabled ? nextOscillator() * 0.16f : 0.0f;
            case Demo::adsr:           return nextADSR() * 0.25f;
            case Demo::filter:         return toneEnabled ? nextFilter() * 0.2f : 0.0f;
            case Demo::noise:          return toneEnabled ? nextNoise() : 0.0f;
            case Demo::downsampler:    return nextDownsampler();
            case Demo::delay:          return nextDelay();
            case Demo::oversampler:    return 0.0f;
            case Demo::voicePool:      return nextVoicePool() * 0.15f;
            case Demo::monoPortamento: return nextMonoPortamento() * 0.18f;
            case Demo::basicSynth:     return nextBasicSynth() * 0.25f;
        }

        return 0.0f;
    }

    float nextOscillator() noexcept
    {
        switch (oscillatorSegment)
        {
            case 0:  return naiveSaw.next();
            case 1:  return naivePulse.next();
            case 2:  return naiveTriangle.next();
            case 3:  return sine.next();
            case 4:  return polySaw.next();
            case 5:  return polyPulse.next();
            case 6:  return polyTriangle.next();
            case 7:  return ellipticSaw.next();
            case 8:  return ellipticPulse.next();
            case 9:  return ellipticTriangle.next();
        }

        return 0.0f;
    }

    float nextADSR() noexcept
    {
        return sine.next() * envelope.next();
    }

    float nextFilter() noexcept
    {
        const auto input = ellipticSaw.next();
        if (filterBypassed)
            return input;

        if (filterOutput == 4)
            return filter.processAll (input).notch;

        return filter.process (input);
    }

    float nextNoise() noexcept
    {
        return noise.next()
             * noiseLevelControl.load (std::memory_order_relaxed);
    }

    float nextDownsampler() noexcept
    {
        if (! toneEnabled)
            return 0.0f;

        const auto drive = driveControl.load (std::memory_order_relaxed);

        if (downsampleMode == 0)
            return std::tanh (samplingLowSine.next() * drive) * 0.2f;

        std::array<float, 4> samples {};
        for (auto& sample : samples)
            sample = std::tanh (samplingHighSine.next() * drive) * 0.2f;

        return downsampler.process (samples);
    }

    float nextDelay() noexcept
    {
        const auto dry = delayBurstSamples > 0 ? sine.next() : 0.0f;
        if (delayBurstSamples > 0)
            --delayBurstSamples;
        const auto delayed = delay.read();
        const auto feedback = feedbackControl.load (std::memory_order_relaxed);
        const auto wet = wetControl.load (std::memory_order_relaxed);
        delay.write (dry + delayed * feedback);
        return (dry * (1.0f - wet) + delayed * wet) * 0.25f;
    }

    void processOversampler (
        const choc::audio::AudioMIDIBlockDispatcher::Block& block,
        bool replaceOutput) noexcept
    {
        const auto frames = block.audioOutput.getNumFrames();

        if (! toneEnabled)
        {
            for (choc::buffer::FrameCount frame = 0; frame < frames; ++frame)
                for (choc::buffer::ChannelCount channel = 0;
                     channel < block.audioOutput.getNumChannels(); ++channel)
                    block.audioOutput.getSample (channel, frame) =
                        replaceOutput ? 0.0f
                                       : block.audioOutput.getSample (channel, frame);
            return;
        }

        if (frames > maxBlockSize)
        {
            const auto drive = driveControl.load (std::memory_order_relaxed);
            for (choc::buffer::FrameCount frame = 0; frame < frames; ++frame)
            {
                const auto sample = std::tanh (
                    samplingLowSine.next() * drive) * 0.2f;
                for (choc::buffer::ChannelCount channel = 0;
                     channel < block.audioOutput.getNumChannels(); ++channel)
                {
                    auto& destination = block.audioOutput.getSample (channel, frame);
                    destination = replaceOutput ? sample : destination + sample;
                }

            }
            return;
        }

        const auto drive = driveControl.load (std::memory_order_relaxed);

        if (oversampleMode == 0)
        {
            for (choc::buffer::FrameCount frame = 0; frame < frames; ++frame)
            {
                const auto sample = std::tanh (
                    samplingLowSine.next() * drive) * 0.2f;
                for (choc::buffer::ChannelCount channel = 0;
                     channel < block.audioOutput.getNumChannels(); ++channel)
                {
                    auto& destination = block.audioOutput.getSample (channel, frame);
                    destination = replaceOutput ? sample : destination + sample;
                }
            }
            return;
        }

        for (uint32_t frame = 0; frame < frames; ++frame)
            oversampleInput[frame] = samplingLowSine.next();

        std::array<const float*, 1> inputChannels { oversampleInput.data() };
        std::array<float*, 1> outputChannels { oversampleOutput.data() };
        const auto input = choc::buffer::createChannelArrayView (
            inputChannels.data(), 1, frames);
        const auto output = choc::buffer::createChannelArrayView (
            outputChannels.data(), 1, frames);
        auto& processor = oversampleMode == 1 ? oversampler2x
                                               : oversampler4x;
        auto upsampled = processor.upsample (input);

        for (uint32_t frame = 0; frame < upsampled.getNumFrames(); ++frame)
            upsampled.getSample (0, frame) =
                std::tanh (upsampled.getSample (0, frame) * drive) * 0.2f;

        processor.downsample (output);

        for (uint32_t frame = 0; frame < frames; ++frame)
        {
            const auto sample = oversampleOutput[frame];
            for (choc::buffer::ChannelCount channel = 0;
                 channel < block.audioOutput.getNumChannels(); ++channel)
            {
                auto& destination = block.audioOutput.getSample (channel, frame);
                destination = replaceOutput ? sample : destination + sample;
            }

        }
    }

    float nextVoicePool() noexcept
    {
        float output = 0.0f;
        withVoicePool ([&output] (auto& pool) { output = pool.next(); });
        return output;
    }

    void applyMonoAction (const MonoController::Action& action) noexcept
    {
        if (action.hasPitch())
        {
            portamento.setTarget (action.pitch, action.glide);
            if (action.trigger)
                monoVoice.start (action.pitch, action.velocity);
        }
        else if (action.shouldRelease())
        {
            monoVoice.release();
        }
    }

    float nextMonoPortamento() noexcept
    {
        applyMonoAction (monoController.next());
        monoVoice.setPitch (portamento.next());
        return monoVoice.next();
    }

    float nextBasicSynth() noexcept
    {
        return synthVoices.next();
    }
};

bool hasOutputDevice()
{
    try
    {
        RtAudio audio;
        for (const auto deviceID : audio.getDeviceIds())
            if (audio.getDeviceInfo (deviceID).outputChannels > 0)
                return true;
    }
    catch (const std::exception&)
    {
    }

    return false;
}

Demo demoForMenuIndex (int64_t index) noexcept
{
    return index == 0 ? Demo::basicSynth
         : index >= 1 && index <= 9 ? static_cast<Demo> (index)
                                    : Demo::basicSynth;
}

class DemoWindow
{
public:
    DemoWindow (DemoCallback& nextCallback, std::string nextAudioStatus)
        : callback (nextCallback), audioStatus (std::move (nextAudioStatus)),
          window ({ 100, 100, 720, 620 })
    {
        setupWindow();
        setupWebView();
    }

    void run()
    {
        window.setVisible (true);
        window.toFront();
        choc::messageloop::run();
    }

private:
    DemoCallback& callback;
    std::string audioStatus;
    choc::ui::DesktopWindow window;
    std::unique_ptr<choc::ui::WebView> webView;

    void setupWindow()
    {
        window.setWindowTitle ("chardsp demos");
        window.centreWithSize (720, 620);
        window.setResizable (true);
        window.setMinimumSize (480, 400);
        window.setVisible (false);
        window.windowClosed = [] { choc::messageloop::stop(); };
    }

    void setupWebView()
    {
        choc::ui::WebView::Options options;
        options.enableDebugMode = true;
        options.enableDebugInspector = true;
        options.acceptsFirstMouseClick = true;
        options.fetchResource = [this] (const std::string& path)
            -> std::optional<choc::ui::WebView::Options::Resource>
        {
            if (path == "/")
                return choc::ui::WebView::Options::Resource (getHTML(), "text/html");

            if (path != "/compost-piano.js"
                && path != "/midi.js"
                && path != "/utils.js")
                return std::nullopt;

            std::ifstream stream (std::string (CHARDSP_DEMO_ASSET_DIR) + path,
                                  std::ios::binary);
            if (! stream)
                return std::nullopt;

            const std::string source ((std::istreambuf_iterator<char> (stream)),
                                      std::istreambuf_iterator<char>());
            return choc::ui::WebView::Options::Resource (source,
                                                         "text/javascript");
        };
        options.webviewIsReady = [this] (choc::ui::WebView& view)
        {
            view.bind ("chardsp_selectDemo",
                       [this] (const choc::value::ValueView& args)
                       -> choc::value::Value
            {
                if (! args.isArray() || args.size() == 0)
                    return {};

                const auto index = args[0].getWithDefault<int64_t> (-1);
                if (index < 0 || index > 9)
                    return {};

                const auto demo = demoForMenuIndex (index);
                callback.selectDemo (demo);
                return {};
            });

            view.bind ("chardsp_setControl",
                       [this] (const choc::value::ValueView& args)
                       -> choc::value::Value
            {
                if (! args.isArray() || args.size() < 2)
                    return {};

                const auto name = args[0].getWithDefault<std::string> ("");
                const auto value = args[1].getWithDefault<double> (0.0);
                callback.setControl (name, static_cast<float> (value));
                return {};
            });

            view.bind ("chardsp_action",
                       [this] (const choc::value::ValueView& args)
                       -> choc::value::Value
            {
                if (! args.isArray() || args.size() == 0)
                    return choc::value::Value (false);

                const auto name = args[0].getWithDefault<std::string> ("");
                const auto value = args.size() > 1
                    ? args[1].getWithDefault<int64_t> (0) : 0;
                return choc::value::Value (callback.queueAction (
                    name, static_cast<int> (value)));
            });

            view.navigate ({});
        };

        webView = std::make_unique<choc::ui::WebView> (options);
        window.setContent (webView->getViewHandle());
    }

    std::string getHTML() const
    {
        return std::string (R"PAGE(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>chardsp demos</title>
<style>
:root { color-scheme: dark; }
[hidden] { display: none !important; }
body { margin: 0; background: #111; color: #ddd; font: 14px -apple-system, BlinkMacSystemFont, sans-serif; }
main { max-width: 680px; margin: 0 auto; padding: 12px; }
section { background: #181818; border: 1px solid #333; padding: 12px; }
h1, h2, p { margin-top: 0; }
h1 { font-size: 20px; font-weight: 600; }
h2 { font-size: 18px; margin-bottom: 6px; }
.status { color: #999; }
.status { margin-bottom: 10px; }
button { display: block; width: 100%; min-height: 38px; padding: 7px 9px; border: 1px solid #555; background: #222; color: #ddd; text-align: left; cursor: pointer; }
button:hover { background: #2b2b2b; }
.picker, .choice, .toggle { display: flex; align-items: center; gap: 6px; }
.picker select, .choice select { padding: 6px; }
.demo-header { display: flex; align-items: center; gap: 10px; margin-bottom: 6px; }
.demo-header h2 { margin: 0; }
.back { width: auto; min-height: 0; }
#selected-description { line-height: 1.35; margin-bottom: 12px; }
.actions { display: flex; flex-wrap: wrap; gap: 6px; margin-bottom: 12px; }
.action { min-height: 0; width: auto; padding: 7px 9px; text-align: center; }
compost-piano { flex-basis: 100%; --compost-piano-height: 122px; --piano-natural-background: #ddd; --piano-natural-border: #555; --piano-accidental-background: #111; --piano-accidental-border: #555; --piano-active-background: #6687c5; --compost-piano-color-scheme: dark; }
compost-piano[hidden] { display: none; }
body.has-piano main { padding-bottom: 134px; }
.controls { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 8px 12px; }
.control-heading { grid-column: 1 / -1; margin: 5px 0 -1px; color: #aaa; font-size: 13px; font-weight: 600; }
.control { display: grid; grid-template-columns: 105px 1fr 52px; gap: 6px; align-items: center; }
.control input[type="range"] { width: 100%; accent-color: #8ab4f8; }
.control input[type="number"] { width: 4.5em; padding: 5px; }
.control select { grid-column: 2 / 4; padding: 5px; }
.control output { color: #999; text-align: right; }
@media (max-width: 540px) {
  .controls { grid-template-columns: 1fr; }
  .control { grid-template-columns: 120px 1fr 56px; }
}
</style>
</head>
<body>
<main>
<section id="picker-view">
<h1>chardsp demos</h1>
<p class="status" id="audio-status">)PAGE") + audioStatus + R"PAGE(</p>
<label class="picker">Demo:
<select id="demo-picker" onchange="if (this.value !== '') selectDemo(Number(this.value))">
<option value="">Choose a demo...</option>
<option value="1">1. Oscillators</option>
<option value="2">2. ADSR envelope</option>
<option value="3">3. State-variable filter</option>
<option value="4">4. Colored noise</option>
<option value="5">5. 4x downsampler</option>
<option value="6">6. Delay line</option>
<option value="7">7. Block oversampler</option>
<option value="8">8. Voice pool</option>
<option value="9">9. Mono portamento</option>
<option value="0">0. Polyphonic basic synth</option>
</select>
</label>
</section>
<section id="demo-view" hidden>
<div class="demo-header">
<button class="back" type="button" onclick="showPicker()">&larr; Demos</button>
<h2 id="selected-title"></h2>
</div>
<p id="selected-description"></p>
<p class="status" id="native-status" hidden></p>
<div class="actions" id="actions"></div>
<div class="controls" id="controls"></div>
</section>
</main>
<compost-piano id="piano" dock hidden></compost-piano>
<script type="module" src="/compost-piano.js"></script>
<script>
const demos = {
  1: {
    title: "Oscillators",
    description: "Compare naive, PolyBLEP, elliptic BLEP, and sine oscillators. The shared waveform control appears only for pulse width, where it has an effect."
  },
  2: {
    title: "ADSR envelope",
    description: "Trigger starts from zero, retrigger starts from the current level, and release follows the selected envelope curve."
  },
  3: {
    title: "State-variable filter",
    description: "Compare the direct signal with lowpass, bandpass, highpass, and notch outputs from one state-variable filter."
  },
  4: {
    title: "Colored noise",
    description: "Switch between brown, pink, white, and blue noise from the same generator."
  },
  5: {
    title: "4x downsampler",
    description: "A/B the same driven sine at direct 1x and external 4x rendering plus downsampling."
  },
  6: {
    title: "Delay line",
    description: "Inject sine bursts manually, then change delay time, feedback, and wet mix."
  },
  7: {
    title: "Block oversampler",
    description: "A/B the same driven sine through 1x, 2x, and 4x block processing."
  },
  8: {
    title: "Voice pool",
    description: "Hold more sine notes than the voice limit. The oldest releasing voice is stolen first, then the oldest held voice. A stolen voice uses the short steal fade before its replacement starts."
  },
  9: {
    title: "Mono portamento",
    description: "Hold overlapping sine notes to test legato retriggering, glide modes, newest-note priority, release fallback, sustain, and note-off grace time."
  },
  0: {
    title: "Polyphonic basic synth",
    description: "Play eight BasicSynthVoice instances through VoicePool. Choose both oscillator shapes, then adjust their mix, pulse width when relevant, noise, ADSR, filter, and velocity response. Velocity amount sets how strongly velocity affects level; exponent 1 is linear, below 1 lifts soft notes, and above 1 makes them quieter. MIDI also works."
  }
};

const controlDefinitions = {
  frequency: { min: 40, max: 12000, step: 1, value: 220, unit: " Hz", precision: 0 },
  samplingFrequency: { min: 100, max: 12000, step: 10, value: 9000, unit: " Hz", precision: 0 },
  drive: { min: 1, max: 20, step: 0.1, value: 3, unit: " x", precision: 1 },
  shape: { min: 0, max: 1, step: 0.01, value: 0.5, unit: "", precision: 2 },
  cutoff: { min: 20, max: 20000, step: 10, value: 4000, unit: " Hz", precision: 0 },
  q: { min: 0.1, max: 10, step: 0.1, value: 0.8, unit: " Q", precision: 1 },
  feedback: { min: 0, max: 0.9, step: 0.01, value: 0.55, unit: "", precision: 2 },
  attack: { min: 0, max: 2, step: 0.01, value: 0.01, unit: " s", precision: 2 },
  decay: { min: 0, max: 2, step: 0.01, value: 0.18, unit: " s", precision: 2 },
  sustain: { min: 0, max: 1, step: 0.01, value: 0.62, unit: "", precision: 2 },
  release: { min: 0, max: 3, step: 0.01, value: 0.6, unit: " s", precision: 2 },
  noiseLevel: { min: 0, max: 0.3, step: 0.01, value: 0.12, unit: "", precision: 2 },
  delayTime: { min: 0, max: 0.95, step: 0.001, value: 0.25, unit: " s", precision: 3 },
  wet: { min: 0, max: 1, step: 0.01, value: 0.7, unit: "", precision: 2 },
  burstLength: { min: 0.01, max: 0.5, step: 0.01, value: 0.1, unit: " s", precision: 2 },
  velocity: { min: 0, max: 1, step: 0.01, value: 0.8, unit: "", precision: 2 },
  portamento: { min: 0, max: 1, step: 0.01, value: 0.18, unit: " s", precision: 2 },
  portamentoShape: { min: 0, max: 1, step: 0.01, value: 0.5, unit: "", precision: 2 },
  timeRateMorph: { min: 0, max: 1, step: 0.01, value: 0.5, unit: "", precision: 2 },
  graceTime: { min: 0, max: 0.25, step: 0.001, value: 0.024, unit: " ms", displayMultiplier: 1000, precision: 0 },
  stealFade: { min: 0, max: 0.01, step: 0.0001, value: 0.002, unit: " ms", displayMultiplier: 1000, precision: 1 },
  voices: { min: 1, max: 4, step: 1, value: 2, unit: "", precision: 0 },
  osc1Level: { min: 0, max: 1, step: 0.01, value: 0.7, unit: "", precision: 2 },
  osc2Level: { min: 0, max: 1, step: 0.01, value: 0.3, unit: "", precision: 2 },
  osc1PulseWidth: { min: 0.02, max: 0.98, step: 0.01, value: 0.5, unit: "", precision: 2 },
  osc2PulseWidth: { min: 0.02, max: 0.98, step: 0.01, value: 0.5, unit: "", precision: 2 },
  synthNoise: { min: 0, max: 0.3, step: 0.01, value: 0.02, unit: "", precision: 2 },
  velocitySensitivity: { min: 0, max: 1, step: 0.01, value: 1, unit: "", precision: 2 },
  velocityCurve: { min: 0.1, max: 4, step: 0.1, value: 1, unit: "", precision: 1 }
};

const controlValues = {};
Object.entries(controlDefinitions).forEach(([name, spec]) => {
  controlValues[name] = spec.value;
});

const controlsForDemo = {
  1: {
    items: [
      { name: "frequency", label: "Frequency", section: "Source" },
      { name: "shape", label: "Shape", section: "Waveform control" }
    ]
  },
  2: {
    items: [
      { name: "frequency", label: "Sine frequency", max: 2000, section: "Test tone" },
      { name: "attack", label: "Attack", section: "Envelope times and level" },
      { name: "decay", label: "Decay" },
      { name: "sustain", label: "Sustain level" },
      { name: "release", label: "Release" }
    ]
  },
  3: {
    items: [
      { name: "frequency", label: "Saw frequency", max: 2000, section: "Source" },
      { name: "cutoff", label: "Cutoff", section: "Filter" },
      { name: "q", label: "Resonance (Q)", section: "Filter" }
    ]
  },
  4: {
    items: [{ name: "noiseLevel", label: "Output level" }]
  },
  5: {
    items: [
      { name: "samplingFrequency", label: "Input frequency" },
      { name: "drive", label: "Waveshaper drive" }
    ]
  },
  6: {
    items: [
      { name: "frequency", label: "Burst frequency", max: 2000 },
      { name: "burstLength", label: "Burst length" },
      { name: "delayTime", label: "Delay time" },
      { name: "feedback", label: "Feedback amount" },
      { name: "wet", label: "Wet mix" }
    ]
  },
  7: {
    items: [
      { name: "samplingFrequency", label: "Input frequency" },
      { name: "drive", label: "Waveshaper drive" }
    ]
  },
  8: {
    items: [
      { name: "voices", label: "Voice limit", type: "number", section: "Allocation" },
      { name: "stealFade", label: "Steal fade time", section: "Allocation" },
      { name: "release", label: "Normal release time", section: "Voice envelope" },
      { name: "velocity", label: "Note-on velocity", section: "Voice envelope" }
    ]
  },
  9: {
    items: [
      { name: "portamento", label: "Base glide time", section: "Portamento" },
      { name: "portamentoShape", label: "Curve (linear–exponential)", section: "Portamento" },
      { name: "timeRateMorph", label: "Time scaling (constant time–constant rate)", section: "Portamento" },
      { name: "graceTime", label: "Note-off grace time", section: "Note policy" },
      { name: "release", label: "Voice release time", section: "Test voice" },
      { name: "velocity", label: "Note-on velocity", section: "Test voice" }
    ]
  },
  0: {
    items: [
      { name: "velocity", label: "Played-note velocity", section: "Input" },
      { name: "osc1Level", label: "Oscillator 1 level", section: "Voice mix" },
      { name: "osc1PulseWidth", label: "Oscillator 1 pulse width" },
      { name: "osc2Level", label: "Oscillator 2 level" },
      { name: "osc2PulseWidth", label: "Oscillator 2 pulse width" },
      { name: "synthNoise", label: "Noise level" },
      { name: "attack", label: "Attack", section: "Envelope" },
      { name: "decay", label: "Decay" },
      { name: "sustain", label: "Sustain level" },
      { name: "release", label: "Release", section: "Envelope" },
      { name: "cutoff", label: "Filter cutoff", section: "Filter" },
      { name: "q", label: "Resonance (Q)", section: "Filter" },
      { name: "velocitySensitivity", label: "Velocity amount", section: "Expression" },
      { name: "velocityCurve", label: "Velocity curve exponent (1 = linear)" }
    ]
  }
};

const actionsForDemo = {
  1: {
    buttons: [
      { label: "Naive saw", action: "setOscillator", value: 0, group: "oscillator", selected: true },
      { label: "Naive pulse", action: "setOscillator", value: 1, group: "oscillator" },
      { label: "Naive triangle", action: "setOscillator", value: 2, group: "oscillator" },
      { label: "Sine", action: "setOscillator", value: 3, group: "oscillator" },
      { label: "PolyBLEP saw", action: "setOscillator", value: 4, group: "oscillator" },
      { label: "PolyBLEP pulse", action: "setOscillator", value: 5, group: "oscillator" },
      { label: "PolyBLEP triangle", action: "setOscillator", value: 6, group: "oscillator" },
      { label: "Elliptic saw", action: "setOscillator", value: 7, group: "oscillator" },
      { label: "Elliptic pulse", action: "setOscillator", value: 8, group: "oscillator" },
      { label: "Elliptic triangle", action: "setOscillator", value: 9, group: "oscillator" },
      { label: "Descending", action: "setSawDirection", value: 0, group: "sawDirection", selected: true },
      { label: "Ascending", action: "setSawDirection", value: 1, group: "sawDirection" },
      { label: "Reset phase", action: "resetWaveform" },
      { label: "Off", action: "setToneEnabled", value: 0, group: "tone" },
      { label: "On", action: "setToneEnabled", value: 1, group: "tone", selected: true }
    ]
  },
  2: {
    buttons: [
      { label: "Trigger note", action: "triggerEnvelope" },
      { label: "Retrigger current level", action: "retriggerEnvelope" },
      { label: "Release note", action: "releaseEnvelope" },
      { label: "Exponential", action: "setEnvelopeShape", value: 0, group: "envelopeShape", selected: true },
      { label: "Linear", action: "setEnvelopeShape", value: 1, group: "envelopeShape" }
    ]
  },
  3: {
    buttons: [
      { label: "Bypass", action: "setFilterMode", value: 0, group: "filterMode" },
      { label: "Lowpass", action: "setFilterMode", value: 1, group: "filterMode", selected: true },
      { label: "Bandpass", action: "setFilterMode", value: 2, group: "filterMode" },
      { label: "Highpass", action: "setFilterMode", value: 3, group: "filterMode" },
      { label: "Notch", action: "setFilterMode", value: 4, group: "filterMode" },
      { label: "Off", action: "setToneEnabled", value: 0, group: "tone" },
      { label: "On", action: "setToneEnabled", value: 1, group: "tone", selected: true }
    ]
  },
  4: {
    buttons: [
      { label: "Brown", action: "setNoiseColor", value: 0, group: "noiseColor", selected: true },
      { label: "Pink", action: "setNoiseColor", value: 1, group: "noiseColor" },
      { label: "White", action: "setNoiseColor", value: 2, group: "noiseColor" },
      { label: "Blue", action: "setNoiseColor", value: 3, group: "noiseColor" },
      { label: "Reset same seed", action: "resetNoise" },
      { label: "Off", action: "setToneEnabled", value: 0, group: "tone" },
      { label: "On", action: "setToneEnabled", value: 1, group: "tone", selected: true }
    ]
  },
  5: {
    buttons: [
      { label: "1x direct (aliased)", action: "setDownsampleMode", value: 0, group: "downsamplePath" },
      { label: "4x + Downsampler4x", action: "setDownsampleMode", value: 1, group: "downsamplePath", selected: true },
      { label: "Reset phase + filter", action: "resetDownsampler" },
      { label: "Off", action: "setToneEnabled", value: 0, group: "tone" },
      { label: "On", action: "setToneEnabled", value: 1, group: "tone", selected: true }
    ]
  },
  6: {
    buttons: [
      { label: "Trigger burst", action: "triggerDelay" },
      { label: "Clear delay", action: "clearDelay" }
    ]
  },
  7: {
    buttons: [
      { label: "1x direct", action: "setOversampleMode", value: 0, group: "oversamplePath" },
      { label: "2x oversampled", action: "setOversampleMode", value: 1, group: "oversamplePath" },
      { label: "4x oversampled", action: "setOversampleMode", value: 2, group: "oversamplePath", selected: true },
      { label: "Reset phase + FIR", action: "resetOversampler" },
      { label: "Off", action: "setToneEnabled", value: 0, group: "tone" },
      { label: "On", action: "setToneEnabled", value: 1, group: "tone", selected: true }
    ]
  },
  8: {
    buttons: [
      { label: "Off", action: "setVoiceSustain", value: 0, group: "voiceSustain", selected: true },
      { label: "On", action: "setVoiceSustain", value: 1, group: "voiceSustain" },
      { label: "Release all notes", action: "voiceReleaseAll", clearNotes: true, resetGroup: "voiceSustain" }
    ],
    notes: {
      on: "voiceNoteOn",
      off: "voiceNoteOff",
      root: 60,
      count: 25
    }
  },
  9: {
    buttons: [
      { label: "On", action: "setMonoLegato", value: 1, group: "legato", selected: true },
      { label: "Off", action: "setMonoLegato", value: 0, group: "legato" },
      { label: "Off", action: "setMonoPortamentoMode", value: 0, group: "portamentoMode" },
      { label: "Always", action: "setMonoPortamentoMode", value: 1, group: "portamentoMode", selected: true },
      { label: "Fingered", action: "setMonoPortamentoMode", value: 2, group: "portamentoMode" },
      { label: "Off", action: "setMonoSustain", value: 0, group: "monoSustain", selected: true },
      { label: "On", action: "setMonoSustain", value: 1, group: "monoSustain" },
      { label: "All notes off", action: "monoAllOff", clearNotes: true, resetGroup: "monoSustain" }
    ],
    notes: {
      on: "monoNoteOn",
      off: "monoNoteOff",
      root: 60,
      count: 25
    }
  },
  0: {
    buttons: [
      { label: "Saw", action: "setSynthOscillator1Waveform", value: 0, group: "osc1Wave", selected: true },
      { label: "Pulse", action: "setSynthOscillator1Waveform", value: 1, group: "osc1Wave" },
      { label: "Triangle", action: "setSynthOscillator1Waveform", value: 2, group: "osc1Wave" },
      { label: "Sine", action: "setSynthOscillator1Waveform", value: 3, group: "osc1Wave" },
      { label: "Saw", action: "setSynthOscillator2Waveform", value: 0, group: "osc2Wave" },
      { label: "Pulse", action: "setSynthOscillator2Waveform", value: 1, group: "osc2Wave" },
      { label: "Triangle", action: "setSynthOscillator2Waveform", value: 2, group: "osc2Wave" },
      { label: "Sine", action: "setSynthOscillator2Waveform", value: 3, group: "osc2Wave", selected: true },
      { label: "Brown", action: "setSynthNoiseColor", value: 0, group: "synthNoise" },
      { label: "Pink", action: "setSynthNoiseColor", value: 1, group: "synthNoise" },
      { label: "White", action: "setSynthNoiseColor", value: 2, group: "synthNoise", selected: true },
      { label: "Blue", action: "setSynthNoiseColor", value: 3, group: "synthNoise" },
      { label: "Exponential", action: "setSynthEnvelopeShape", value: 0, group: "synthEnvelopeShape", selected: true },
      { label: "Linear", action: "setSynthEnvelopeShape", value: 1, group: "synthEnvelopeShape" },
      { label: "Lowpass", action: "setSynthFilterMode", value: 0, group: "synthFilterMode", selected: true },
      { label: "Bandpass", action: "setSynthFilterMode", value: 1, group: "synthFilterMode" },
      { label: "Highpass", action: "setSynthFilterMode", value: 2, group: "synthFilterMode" },
      { label: "Release all notes", action: "synthReleaseAll", clearNotes: true }
    ],
    notes: {
      on: "synthNoteOn",
      off: "synthNoteOff",
      root: 60,
      count: 25
    }
  }
};

let currentDemo = null;
const piano = document.querySelector("#piano");
piano.addEventListener("note-down", event =>
  sendAction(piano.dataset.noteOn, event.detail.note));
piano.addEventListener("note-up", event =>
  sendAction(piano.dataset.noteOff, event.detail.note));

function showStatus(message) {
  const status = document.querySelector("#native-status");
  status.textContent = message;
  status.hidden = false;
}

function sendAction(name, value = 0) {
  if (typeof window.chardsp_action !== "function") {
    showStatus("C++ action binding unavailable.");
    return;
  }

  const result = window.chardsp_action(name, value);
  if (result && typeof result.then === "function")
    result.then(accepted => {
      if (!accepted)
        showStatus("Action queue is full; try again.");
    }).catch(() => {
      showStatus("C++ action binding unavailable.");
    });
}

function clearNoteButtons() {
  piano.allNotesOff?.();
}

const groupLabels = {
  oscillator: "Oscillator",
  sawDirection: "Saw direction",
  envelopeShape: "Shape",
  filterMode: "Mode",
  noiseColor: "Colour",
  downsamplePath: "Path",
  oversamplePath: "Rate",
  tone: "Output",
  voiceSustain: "Sustain",
  legato: "Legato",
  portamentoMode: "Portamento",
  monoSustain: "Sustain",
  osc1Wave: "Oscillator 1 shape",
  osc2Wave: "Oscillator 2 shape",
  synthNoise: "Noise",
  synthEnvelopeShape: "Envelope shape",
  synthFilterMode: "Filter mode"
};

function renderActions(index) {
  const config = actionsForDemo[index];
  const container = document.querySelector("#actions");
  container.textContent = "";
  const renderedGroups = new Set();

  config.buttons.forEach(item => {
    if (item.group) {
      if (renderedGroups.has(item.group))
        return;

      renderedGroups.add(item.group);
      const choices = config.buttons.filter(choice => choice.group === item.group);
      const offChoice = choices.find(choice => choice.label === "Off");
      const onChoice = choices.find(choice => choice.label === "On");
      const label = document.createElement("label");

      if (choices.length === 2 && offChoice && onChoice) {
        label.className = "toggle";
        const checkbox = document.createElement("input");
        checkbox.type = "checkbox";
        checkbox.checked = Boolean(onChoice.selected);
        checkbox.dataset.group = item.group;
        checkbox.dataset.default = checkbox.checked;
        checkbox.addEventListener("change", () => {
          const choice = checkbox.checked ? onChoice : offChoice;
          sendAction(choice.action, choice.value !== undefined ? choice.value : 0);
        });
        label.append(checkbox, document.createTextNode(groupLabels[item.group]));
        container.append(label);
        return;
      }

      label.className = "choice";
      label.append(document.createTextNode(groupLabels[item.group] + ":"));
      const select = document.createElement("select");
      select.dataset.group = item.group;

      choices.forEach((choice, choiceIndex) => {
        const option = document.createElement("option");
        option.value = choiceIndex;
        option.textContent = choice.label;
        option.selected = Boolean(choice.selected);
        select.append(option);
      });

      select.dataset.default = select.value;
      select.addEventListener("change", () => {
        const choice = choices[Number(select.value)];
        sendAction(choice.action, choice.value !== undefined ? choice.value : 0);
        if (item.group === "oscillator")
          showOscillatorControls(choice.value);
        if (item.group === "osc1Wave" || item.group === "osc2Wave")
          showSynthOscillatorControls();
      });
      label.append(select);
      container.append(label);
      return;
    }

    const button = document.createElement("button");
    button.className = "action";
    button.type = "button";
    button.textContent = item.label;
    button.addEventListener("click", () => {
      if (item.clearNotes)
        clearNoteButtons();
      if (item.resetGroup) {
        const choice = container.querySelector('[data-group="'
                                               + item.resetGroup + '"]');
        if (choice?.type === "checkbox")
          choice.checked = choice.dataset.default === "true";
        else if (choice)
          choice.value = choice.dataset.default;
      }
      sendAction(item.action, item.value !== undefined ? item.value : 0);
    });
    container.append(button);
  });

  piano.hidden = !config.notes;
  if (config.notes) {
    piano.setAttribute("root-note", config.notes.root);
    piano.setAttribute("note-count", config.notes.count);
    piano.dataset.noteOn = config.notes.on;
    piano.dataset.noteOff = config.notes.off;
  }

}

function renderControls(index) {
  const config = controlsForDemo[index];
  const container = document.querySelector("#controls");
  container.textContent = "";
  let currentSection = null;

  config.items.forEach(item => {
    if (item.section && item.section !== currentSection) {
      currentSection = item.section;
      const heading = document.createElement("h3");
      heading.className = "control-heading";
      heading.textContent = item.section;
      heading.dataset.controlSection = item.section;
      container.append(heading);
    }

    const spec = controlDefinitions[item.name];
    const row = document.createElement("label");
    row.className = "control";
    row.dataset.controlRow = item.name;

    const label = document.createElement("span");
    label.textContent = item.label;

    const input = document.createElement(item.values ? "select" : "input");
    if (item.values) {
      item.values.forEach(value => {
        const option = document.createElement("option");
        option.value = value;
        option.textContent = value;
        input.append(option);
      });
    }
    else {
      input.type = item.type || "range";
      input.min = item.min !== undefined ? item.min : spec.min;
      input.max = item.max !== undefined ? item.max : spec.max;
      input.step = item.step !== undefined ? item.step : spec.step;
    }
    input.value = controlValues[item.name];
    input.dataset.control = item.name;
    input.dataset.unit = spec.unit;
    input.dataset.displayMultiplier = spec.displayMultiplier || 1;
    input.dataset.precision = spec.precision;

    const output = input.type === "range" ? document.createElement("output") : null;
    row.append(label, input);
    if (output)
      row.append(output);
    container.append(row);
    input.addEventListener(input.type === "range" ? "input" : "change",
                           () => updateControl(input));
    updateControl(input);
  });

  if (index === 1) {
    const oscillator = document.querySelector('[data-group="oscillator"]');
    showOscillatorControls(Number(oscillator?.value ?? 0));
  }
  else if (index === 0) {
    showSynthOscillatorControls();
  }
}

function showOscillatorControls(oscillator) {
  const shape = document.querySelector('[data-control-row="shape"]');
  const shapeLabel = shape.querySelector("span");
  const isPulse = [1, 5, 8].includes(oscillator);
  shape.hidden = !isPulse;
  shapeLabel.textContent = isPulse ? "Pulse width" : "Shape";
  document.querySelector('[data-control-section="Waveform control"]').hidden =
    shape.hidden;
  document.querySelector('[data-group="sawDirection"]')
    ?.closest(".choice").toggleAttribute("hidden", ![0, 4, 7].includes(oscillator));
}

function showSynthOscillatorControls() {
  const osc1 = document.querySelector('[data-group="osc1Wave"]');
  const osc2 = document.querySelector('[data-group="osc2Wave"]');
  document.querySelector('[data-control-row="osc1PulseWidth"]').hidden =
    Number(osc1?.value ?? 0) !== 1;
  document.querySelector('[data-control-row="osc2PulseWidth"]').hidden =
    Number(osc2?.value ?? 3) !== 1;
}

async function selectDemo(index) {
  const demo = demos[index];
  currentDemo = index;
  document.body.classList.toggle("has-piano", Boolean(actionsForDemo[index].notes));
  document.querySelector("#picker-view").hidden = true;
  document.querySelector("#demo-view").hidden = false;
  document.querySelector("#native-status").hidden = true;
  document.querySelector("#selected-title").textContent = demo.title;
  document.querySelector("#selected-description").textContent = demo.description;
  renderActions(index);
  renderControls(index);
  try {
    await window.chardsp_selectDemo(index);
  } catch (error) {
    showStatus("C++ binding unavailable.");
  }
}

function showPicker() {
  const stopActions = {
    0: "synthReleaseAll", 1: "setToneEnabled", 2: "forceReleaseEnvelope",
    3: "setToneEnabled", 4: "setToneEnabled", 5: "setToneEnabled", 6: "clearDelay",
    7: "setToneEnabled", 8: "voiceReleaseAll", 9: "monoAllOff"
  };

  if (currentDemo !== null)
    sendAction(stopActions[currentDemo]);

  currentDemo = null;
  piano.hidden = true;
  document.body.classList.remove("has-piano");
  document.querySelector("#demo-picker").value = "";
  document.querySelector("#demo-view").hidden = true;
  document.querySelector("#picker-view").hidden = false;
}

function updateControl(input) {
  const output = input.parentElement.querySelector("output");
  const value = Number(input.value);
  controlValues[input.dataset.control] = value;
  const precision = Number(input.dataset.precision || 0);
  if (output)
    output.textContent = (value * Number(input.dataset.displayMultiplier || 1))
      .toFixed(precision) + (input.dataset.unit || "");
  if (typeof window.chardsp_setControl === "function") {
    const result = window.chardsp_setControl(input.dataset.control, value);
    if (result && typeof result.catch === "function")
      result.catch(() => {
        showStatus("C++ control binding unavailable.");
      });
  }
}

showPicker();
</script>
</body>
</html>
)PAGE";
    }
};

void printHelp()
{
    std::cout
        << "chardsp demo runner opens a CHOC DesktopWindow UI.\n"
        << "Choose a demo from the menu.\n";
}
}

int main (int argc, const char* const* argv)
{
    if (argc > 1 && std::string (argv[1]) == "--help")
    {
        printHelp();
        return 0;
    }

    choc::ui::setWindowsDPIAwareness();
    choc::messageloop::initialise();

    DemoCallback callback;
    std::unique_ptr<choc::audio::io::RtAudioMIDIPlayer> player;
    std::string audioStatus = "Audio unavailable; controls still show each DSP path.";

    if (hasOutputDevice())
    {
        choc::audio::io::AudioDeviceOptions options;
        options.sampleRate = 48000;
        options.blockSize = 256;
        options.outputChannelCount = 2;
        options.inputChannelCount = 0;

        auto logMessage = [] (const std::string& message)
        {
            std::cout << "[audio] " << message << '\n';
        };

        player = std::make_unique<choc::audio::io::RtAudioMIDIPlayer> (
            options, logMessage);

        if (const auto error = player->getLastError(); error.empty())
        {
            player->addCallback (callback);
            audioStatus = "Audio active; MIDI controls the basic synth voice.";
        }
        else
        {
            std::cerr << "Could not open audio: " << error << '\n';
            player.reset();
        }
    }

    DemoWindow window (callback, std::move (audioStatus));
    window.run();

    if (player != nullptr)
        player->removeCallback (callback);

    return 0;
}
