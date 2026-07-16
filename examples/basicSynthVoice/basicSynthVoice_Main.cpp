#include <filesystem>
#include <iostream>

#include "chardsp/chardsp_BasicSynthVoice.h"
#include "choc/audio/choc_AudioFileFormat_WAV.h"
#include "choc/audio/choc_SampleBuffers.h"

int main (int argc, const char* const* argv)
{
    constexpr float sampleRate = 48000.0f;
    constexpr uint32_t frames = 3 * 48000;
    const std::filesystem::path path = argc > 1
        ? argv[1] : "chardsp_basic_synth_voice.wav";

    chardsp::BasicSynthVoice<float> voice;
    voice.prepare (sampleRate, 0);
    voice.setOscillator1Waveform (chardsp::NomadOscillatorWaveform::saw);
    voice.setOscillator1Shape (0.2f);
    voice.setOscillator1Level (0.7f);
    voice.setOscillator2Waveform (chardsp::NomadOscillatorWaveform::sine);
    voice.setOscillator2Shape (0.0f);
    voice.setOscillator2Level (0.25f);
    voice.setNoiseColor (chardsp::NoiseColor::pink);
    voice.setNoiseLevel (0.03f);
    voice.setAttack (0.02f);
    voice.setDecay (0.3f);
    voice.setSustain (0.55f);
    voice.setRelease (0.8f);
    voice.setFilter (chardsp::FilterMode::lowpass, 2400.0f, 1.1f);
    voice.setVelocitySensitivity (1.0f);
    voice.setVelocityCurve (1.0f);
    voice.start (57.0f, 0.75f);

    choc::buffer::ChannelArrayBuffer<float> buffer (2, frames);
    auto output = buffer.getView();

    for (uint32_t frame = 0; frame < frames; ++frame)
    {
        if (frame == 2 * 48000)
            voice.release();

        const auto sample = voice.next() * 0.25f;
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
