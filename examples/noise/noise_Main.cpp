#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <string_view>

#include "chardsp/chardsp_ColoredNoise.h"
#include "choc/audio/choc_AudioFileFormat_WAV.h"
#include "choc/audio/choc_SampleBuffers.h"

int main (int argc, const char* const* argv)
{
    constexpr float sampleRate = 48000.0f;
    constexpr uint32_t segmentFrames = 36000;
    constexpr uint32_t fadeFrames = 240;
    constexpr std::array colors {
        chardsp::NoiseColor::brown,
        chardsp::NoiseColor::pink,
        chardsp::NoiseColor::white,
        chardsp::NoiseColor::blue
    };
    constexpr std::array<std::string_view, 4> labels {
        "brown", "pink", "white", "blue"
    };
    const std::filesystem::path path = argc > 1
        ? argv[1] : "chardsp_noise.wav";

    chardsp::ColoredNoise<float> noise;
    noise.reset (0x12345678u);
    choc::buffer::ChannelArrayBuffer<float> buffer (
        2, segmentFrames * static_cast<uint32_t> (colors.size()));
    auto output = buffer.getView();

    for (uint32_t segment = 0; segment < colors.size(); ++segment)
    {
        noise.setColor (colors[segment]);

        for (uint32_t frame = 0; frame < segmentFrames; ++frame)
        {
            const auto fadeIn = std::min (1.0f,
                static_cast<float> (frame) / static_cast<float> (fadeFrames));
            const auto fadeOut = std::min (1.0f,
                static_cast<float> (segmentFrames - 1 - frame)
                    / static_cast<float> (fadeFrames));
            const auto sample = noise.next() * 0.12f
                              * std::min (fadeIn, fadeOut);
            const auto destination = segment * segmentFrames + frame;

            for (uint32_t channel = 0; channel < output.getNumChannels(); ++channel)
                output.getSample (channel, destination) = sample;
        }
    }

    choc::audio::WAVAudioFileFormat<true> format;
    choc::audio::AudioFileProperties properties;
    properties.sampleRate = sampleRate;
    properties.numFrames = buffer.getNumFrames();
    properties.numChannels = 2;
    properties.bitDepth = choc::audio::BitDepth::int16;
    auto writer = format.createWriter (path, properties);

    if (writer == nullptr || ! writer->appendFrames (buffer.getView())
        || ! writer->flush())
    {
        std::cerr << "Failed to write " << path << '\n';
        return 1;
    }

    std::cout << "Wrote " << path << "\nSegments:\n";
    for (size_t i = 0; i < labels.size(); ++i)
        std::cout << "  " << i + 1 << ". " << labels[i] << '\n';
}
