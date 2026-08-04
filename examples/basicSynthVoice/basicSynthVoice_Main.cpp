#include <filesystem>
#include <iostream>

#include "chardsp/chardsp_BasicSynthVoice.h"
#include "chardsp/chardsp_VoicePool.h"
#include "choc/audio/choc_AudioFileFormat_WAV.h"
#include "choc/audio/choc_SampleBuffers.h"

namespace
{
struct Voice : chardsp::BasicSynthVoice<float>
{
    void start (const int& note, float velocity) noexcept
    {
        chardsp::BasicSynthVoice<float>::start (
            static_cast<float> (note), velocity);
    }
};
}

int main (int argc, const char* const* argv)
{
    constexpr float sampleRate = 48000.0f;
    constexpr uint32_t frames = 4 * 48000;
    const std::filesystem::path path = argc > 1
        ? argv[1] : "chardsp_basic_synth_voice.wav";

    chardsp::VoicePool<Voice, 4, int> voices;
    voices.prepare (sampleRate);
    voices.setStealFadeTime (0.002f);

    for (size_t index = 0; index < 4; ++index)
    {
        auto& voice = voices.getVoice (index);
        voice.setOscillator1Waveform (chardsp::BasicSynthWaveform::saw);
        voice.setOscillator1Level (0.75f);
        voice.setOscillator2Waveform (chardsp::BasicSynthWaveform::triangle);
        voice.setOscillator2Level (0.25f);
        voice.setNoiseLevel (0.015f);
        voice.setAttack (0.01f);
        voice.setDecay (0.18f);
        voice.setSustain (0.62f);
        voice.setRelease (0.45f);
        voice.setFilter (chardsp::FilterMode::lowpass, 4200.0f, 0.8f);
    }

    choc::buffer::ChannelArrayBuffer<float> buffer (2, frames);
    auto output = buffer.getView();

    for (uint32_t frame = 0; frame < frames; ++frame)
    {
        if (frame == 0)
            voices.start (60, 0.8f);
        else if (frame == 12000)
            voices.start (64, 0.75f);
        else if (frame == 24000)
            voices.start (67, 0.9f);
        else if (frame == 36000)
            voices.start (72, 0.8f);
        else if (frame == 72000)
            voices.stop (60);
        else if (frame == 84000)
            voices.stop (64);
        else if (frame == 96000)
            voices.stop (67);
        else if (frame == 108000)
            voices.stop (72);
        else if (frame == 132000)
            voices.start (76, 0.85f);
        else if (frame == 144000)
            voices.start (79, 0.85f);
        else if (frame == 156000)
            voices.start (83, 0.85f);

        const auto sample = voices.next() * 0.18f;
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

    std::cout << "Wrote " << path << "\n";
}
