#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>

#include "chardsp/chardsp_Oscillator.h"
#include "chardsp/chardsp_Oversampler.h"
#include "choc/audio/choc_AudioFileFormat_WAV.h"
#include "choc/audio/choc_SampleBuffers.h"

int main (int argc, const char* const* argv)
{
    constexpr float sampleRate = 48000.0f;
    constexpr uint32_t frames = 2 * 48000;
    constexpr uint32_t blockSize = 64;
    const std::filesystem::path path = argc > 1
        ? argv[1] : "chardsp_oversampler.wav";
    chardsp::SineOscillator<float> oscillator;
    oscillator.prepare (sampleRate);
    oscillator.setFrequency (9000.0f);
    chardsp::Oversampler<float> oversampler;

    if (! oversampler.prepare (
            1, blockSize, chardsp::OversamplingFactor::x4))
        return 1;

    choc::buffer::ChannelArrayBuffer<float> lowRateInput (1, blockSize);
    choc::buffer::ChannelArrayBuffer<float> lowRateOutput (1, blockSize);
    choc::buffer::ChannelArrayBuffer<float> rendered (2, frames);

    for (uint32_t blockStart = 0; blockStart < frames;
         blockStart += blockSize)
    {
        const auto numFrames = std::min (blockSize, frames - blockStart);
        auto input = lowRateInput.getStart (numFrames);
        auto output = lowRateOutput.getStart (numFrames);

        for (uint32_t frame = 0; frame < numFrames; ++frame)
            input.getSample (0, frame) = oscillator.next();

        auto highRate = oversampler.upsample (input);

        for (uint32_t frame = 0; frame < highRate.getNumFrames(); ++frame)
            highRate.getSample (0, frame) = std::tanh (
                highRate.getSample (0, frame) * 3.0f);

        oversampler.downsample (output);

        for (uint32_t frame = 0; frame < numFrames; ++frame)
            for (uint32_t channel = 0; channel < 2; ++channel)
                rendered.getSample (channel, blockStart + frame)
                    = output.getSample (0, frame) * 0.2f;
    }

    choc::audio::WAVAudioFileFormat<true> format;
    choc::audio::AudioFileProperties properties;
    properties.sampleRate = sampleRate;
    properties.numFrames = frames;
    properties.numChannels = 2;
    properties.bitDepth = choc::audio::BitDepth::int16;
    auto writer = format.createWriter (path, properties);

    if (writer == nullptr || ! writer->appendFrames (rendered.getView())
        || ! writer->flush())
    {
        std::cerr << "Failed to write " << path << '\n';
        return 1;
    }

    std::cout << "Wrote " << path << " with 4x nonlinear processing; latency "
              << oversampler.getLatency() << " samples\n";
}
