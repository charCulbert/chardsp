#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>

#include "chardsp/chardsp_Downsampler4x.h"
#include "chardsp/chardsp_Oscillator.h"
#include "choc/audio/choc_AudioFileFormat_WAV.h"
#include "choc/audio/choc_SampleBuffers.h"

int main (int argc, const char* const* argv)
{
    constexpr float sampleRate = 48000.0f;
    constexpr uint32_t frames = 2 * 48000;
    const std::filesystem::path path = argc > 1
        ? argv[1] : "chardsp_downsampler.wav";

    // Oversampling policy is explicit here: prepare the source at 4x, render
    // four nonlinear samples, then pass that frame to Downsampler4x.
    chardsp::SineOscillator<float> oscillator;
    oscillator.prepare (sampleRate * 4.0f);
    oscillator.setFrequency (880.0f);
    chardsp::Downsampler4x<float> downsampler;
    downsampler.reset();

    choc::buffer::ChannelArrayBuffer<float> buffer (2, frames);
    auto output = buffer.getView();

    for (uint32_t frame = 0; frame < frames; ++frame)
    {
        std::array<float, 4> oversampled {};

        for (auto& sample : oversampled)
            sample = std::tanh (oscillator.next() * 3.0f) * 0.2f;

        const auto sample = downsampler.process (oversampled);
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
              << " using an explicitly external 4x waveshaping path\n";
}
