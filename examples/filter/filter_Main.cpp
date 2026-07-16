#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <string_view>

#include "chardsp/chardsp_Oscillator.h"
#include "chardsp/chardsp_StateVariableFilter.h"
#include "choc/audio/choc_AudioFileFormat_WAV.h"
#include "choc/audio/choc_SampleBuffers.h"

int main (int argc, const char* const* argv)
{
    constexpr float sampleRate = 48000.0f;
    constexpr uint32_t segmentFrames = 48000;
    constexpr uint32_t fadeFrames = 240;
    constexpr std::array modes {
        chardsp::FilterMode::lowpass,
        chardsp::FilterMode::bandpass,
        chardsp::FilterMode::highpass
    };
    constexpr std::array<std::string_view, 3> labels {
        "lowpass", "bandpass", "highpass"
    };
    const std::filesystem::path path = argc > 1
        ? argv[1] : "chardsp_filter.wav";

    chardsp::EllipticSawOscillator<float> oscillator;
    oscillator.prepare (sampleRate);
    oscillator.setFrequency (110.0f);
    chardsp::StateVariableFilter<float> filter;
    filter.prepare (sampleRate);
    filter.setCutoffAndQ (1200.0f, 1.2f);

    choc::buffer::ChannelArrayBuffer<float> buffer (
        2, segmentFrames * static_cast<uint32_t> (modes.size()));
    auto output = buffer.getView();

    for (uint32_t segment = 0; segment < modes.size(); ++segment)
    {
        oscillator.reset();
        filter.reset();
        filter.setMode (modes[segment]);

        for (uint32_t frame = 0; frame < segmentFrames; ++frame)
        {
            const auto fadeIn = std::min (1.0f,
                static_cast<float> (frame) / static_cast<float> (fadeFrames));
            const auto fadeOut = std::min (1.0f,
                static_cast<float> (segmentFrames - 1 - frame)
                    / static_cast<float> (fadeFrames));
            const auto sample = filter.process (oscillator.next()) * 0.2f
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
