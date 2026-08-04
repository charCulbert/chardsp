#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>

#include "chardsp/chardsp_ADSREnvelope.h"
#include "chardsp/chardsp_Oscillator.h"
#include "chardsp/chardsp_VoicePool.h"
#include "choc/audio/choc_AudioFileFormat_WAV.h"
#include "choc/audio/choc_SampleBuffers.h"

namespace
{
struct Voice
{
    void prepare (float sampleRate, size_t) noexcept
    {
        oscillator.prepare (sampleRate);
        oscillator.reset();
        envelope.prepare (sampleRate);
        envelope.setAttack (0.005f);
        envelope.setDecay (0.08f);
        envelope.setSustain (0.65f);
        envelope.setRelease (0.25f);
        velocity = 0.0f;
    }

    void start (const int& note, float velocity) noexcept
    {
        oscillator.setFrequency (440.0f * std::exp2 (
            (static_cast<float> (note) - 69.0f) / 12.0f));
        oscillator.reset();
        this->velocity = std::clamp (velocity, 0.0f, 1.0f);
        envelope.trigger();
    }

    void release() noexcept { envelope.release(); }
    void forceRelease (float seconds) noexcept { envelope.forceRelease (seconds); }
    bool isAvailable() const noexcept { return ! envelope.isActive(); }
    void setRelease (float seconds) noexcept { envelope.setRelease (seconds); }

    float next() noexcept
    {
        return envelope.isActive() ? oscillator.next() * envelope.next() * velocity
                                   : 0.0f;
    }

private:
    float velocity = 0.0f;
    chardsp::SineOscillator<float> oscillator;
    chardsp::ADSREnvelope<float> envelope;
};
}

int main (int argc, const char* const* argv)
{
    constexpr float sampleRate = 48000.0f;
    constexpr uint32_t frames = 48000;
    const std::filesystem::path path = argc > 1
        ? argv[1] : "chardsp_voice_pool.wav";

    chardsp::VoicePool<Voice, 2, int> voices;
    voices.prepare (sampleRate);
    voices.setStealFadeTime (0.002f);

    for (size_t i = 0; i < 2; ++i)
        voices.getVoice (i).setRelease (0.25f);

    choc::buffer::ChannelArrayBuffer<float> buffer (2, frames);
    auto output = buffer.getView();

    for (uint32_t frame = 0; frame < frames; ++frame)
    {
        if (frame == 0)
        {
            voices.start (60, 0.8f);
            std::cout << "start 60 -> free voice\n";
        }
        else if (frame == 4800)
        {
            voices.start (64, 0.8f);
            std::cout << "start 64 -> second free voice\n";
        }
        else if (frame == 9600)
        {
            voices.start (67, 1.0f);
            std::cout << "start 67 -> steal fade; pending="
                      << voices.isPending (67) << '\n';
        }
        else if (frame == 12000)
        {
            voices.stop (60);
            std::cout << "stop 60 -> harmless if already stolen\n";
        }
        else if (frame == 24000)
        {
            voices.stop (64);
            std::cout << "stop 64 -> normal release\n";
        }
        else if (frame == 30000)
        {
            voices.stop (67);
            std::cout << "stop 67 -> normal release\n";
        }

        const auto sample = voices.next() * 0.15f;
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

    std::cout << "Wrote " << path << '\n';
}
