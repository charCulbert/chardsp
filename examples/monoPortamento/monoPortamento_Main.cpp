#include <filesystem>
#include <iostream>
#include <algorithm>
#include <cmath>

#include "chardsp/chardsp_ADSREnvelope.h"
#include "chardsp/chardsp_MonoVoiceController.h"
#include "chardsp/chardsp_Oscillator.h"
#include "chardsp/chardsp_Portamento.h"
#include "choc/audio/choc_AudioFileFormat_WAV.h"
#include "choc/audio/choc_SampleBuffers.h"

int main (int argc, const char* const* argv)
{
    constexpr float sampleRate = 48000.0f;
    constexpr uint32_t frames = 72000;
    using Controller = chardsp::MonoVoiceController<float, int, 16>;
    const std::filesystem::path path = argc > 1
        ? argv[1] : "chardsp_mono_portamento.wav";

    Controller controller;
    controller.prepare (sampleRate);
    controller.setLegato (true);
    controller.setPortamentoMode (chardsp::PortamentoMode::fingered);

    chardsp::Portamento<float> portamento;
    portamento.prepare (sampleRate);
    portamento.reset (60.0f);
    portamento.setTime (0.12f);
    portamento.setShape (0.5f);
    portamento.setTimeRateMorph (0.0f);

    struct SineVoice
    {
        void prepare (float newSampleRate) noexcept
        {
            oscillator.prepare (newSampleRate);
            oscillator.reset();
            envelope.prepare (newSampleRate);
            envelope.setAttack (0.01f);
            envelope.setDecay (0.1f);
            envelope.setSustain (0.7f);
            envelope.setRelease (0.3f);
        }

        void start (float pitch, float nextVelocity) noexcept
        {
            setPitch (pitch);
            oscillator.reset();
            velocity = std::clamp (nextVelocity, 0.0f, 1.0f);
            envelope.trigger();
        }

        void setPitch (float pitch) noexcept
        {
            oscillator.setFrequency (440.0f * std::exp2 ((pitch - 69.0f) / 12.0f));
        }

        void setVelocity (float nextVelocity) noexcept
        {
            velocity = std::clamp (nextVelocity, 0.0f, 1.0f);
        }

        void release() noexcept { envelope.release(); }

        float next() noexcept
        {
            return envelope.isActive() ? oscillator.next() * envelope.next() * velocity
                                       : 0.0f;
        }

        chardsp::SineOscillator<float> oscillator;
        chardsp::ADSREnvelope<float> envelope;
        float velocity = 0.0f;
    } voice;
    voice.prepare (sampleRate);

    const auto apply = [&] (const Controller::Action& action)
    {
        if (action.hasPitch())
        {
            portamento.setTarget (action.pitch, action.glide);

            if (action.trigger)
                voice.start (action.pitch, action.velocity);
            else
                voice.setVelocity (action.velocity);
        }
        else if (action.shouldRelease())
        {
            voice.release();
        }
    };

    choc::buffer::ChannelArrayBuffer<float> buffer (2, frames);
    auto output = buffer.getView();

    for (uint32_t frame = 0; frame < frames; ++frame)
    {
        if (frame == 0)
            apply (controller.start (60, 60.0f, 0.8f));
        else if (frame == 12000)
            apply (controller.start (67, 67.0f, 0.9f));
        else if (frame == 24000)
            apply (controller.start (64, 64.0f, 0.7f));
        else if (frame == 32000)
            apply (controller.stop (64));
        else if (frame == 38000)
            apply (controller.stop (67));
        else if (frame == 48000)
            apply (controller.stop (60));

        apply (controller.next());
        voice.setPitch (portamento.next());
        const auto sample = voice.next() * 0.22f;

        for (uint32_t channel = 0; channel < output.getNumChannels(); ++channel)
            output.getSample (channel, frame) = sample;
    }

    choc::audio::WAVAudioFileFormat<true> format;
    choc::audio::AudioFileProperties properties;
    properties.sampleRate = sampleRate;
    properties.numFrames = frames;
    properties.numChannels = 2;
    properties.bitDepth = choc::audio::BitDepth::int16;
    auto writer = format.createWriter (path, properties);

    if (writer == nullptr || ! writer->appendFrames (buffer.getView())
        || ! writer->flush())
    {
        std::cerr << "Failed to write " << path << '\n';
        return 1;
    }

    std::cout << "Wrote " << path
              << " with fingered legato and last-note fallback\n";
}
