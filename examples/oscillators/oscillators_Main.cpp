#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <string_view>

#include "chardsp/chardsp_Oscillator.h"
#include "choc/audio/choc_AudioFileFormat_WAV.h"
#include "choc/audio/choc_SampleBuffers.h"

namespace
{
constexpr float sampleRate = 48000.0f;
constexpr uint32_t segmentFrames = 16800;
constexpr uint32_t fadeFrames = 240;

template <typename Oscillator>
void renderPreparedSegment (Oscillator& oscillator,
                            choc::buffer::ChannelArrayView<float> output,
                            uint32_t startFrame)
{
    for (uint32_t frame = 0; frame < segmentFrames; ++frame)
    {
        const auto fadeIn = std::min (1.0f,
            static_cast<float> (frame) / static_cast<float> (fadeFrames));
        const auto fadeOut = std::min (1.0f,
            static_cast<float> (segmentFrames - 1 - frame)
                / static_cast<float> (fadeFrames));
        const auto sample = oscillator.next() * 0.18f
                          * std::min (fadeIn, fadeOut);

        for (uint32_t channel = 0; channel < output.getNumChannels(); ++channel)
            output.getSample (channel, startFrame + frame) = sample;
    }
}

template <typename Oscillator>
void renderSegment (Oscillator& oscillator,
                    choc::buffer::ChannelArrayView<float> output,
                    uint32_t startFrame)
{
    oscillator.prepare (sampleRate);
    oscillator.setFrequency (220.0f);
    oscillator.reset();
    renderPreparedSegment (oscillator, output, startFrame);
}

bool writeWav (const std::filesystem::path& path,
               const choc::buffer::ChannelArrayBuffer<float>& buffer)
{
    choc::audio::WAVAudioFileFormat<true> format;
    choc::audio::AudioFileProperties properties;
    properties.sampleRate = sampleRate;
    properties.numFrames = buffer.getNumFrames();
    properties.numChannels = buffer.getNumChannels();
    properties.bitDepth = choc::audio::BitDepth::int16;
    auto writer = format.createWriter (path, properties);
    return writer != nullptr && writer->appendFrames (buffer.getView())
        && writer->flush();
}
}

int main (int argc, const char* const* argv)
{
    constexpr std::array<std::string_view, 10> labels {
        "naive saw", "naive pulse", "naive triangle", "sine",
        "PolyBLEP saw", "PolyBLEP pulse", "PolyBLEP triangle",
        "elliptic saw", "elliptic pulse", "elliptic triangle"
    };
    choc::buffer::ChannelArrayBuffer<float> buffer (
        2, segmentFrames * static_cast<uint32_t> (labels.size()));
    buffer.clear();
    auto output = buffer.getView();
    uint32_t segment = 0;

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
    naivePulse.setPulseWidth (0.35f);
    polyPulse.setPulseWidth (0.35f);
    ellipticPulse.setPulseWidth (0.35f);

    const auto render = [&] (auto& oscillator)
    {
        renderSegment (oscillator, output, segment++ * segmentFrames);
    };

    render (naiveSaw);
    render (naivePulse);
    render (naiveTriangle);
    render (sine);
    render (polySaw);
    render (polyPulse);
    render (polyTriangle);
    render (ellipticSaw);
    render (ellipticPulse);
    render (ellipticTriangle);
    const std::filesystem::path path = argc > 1
        ? argv[1] : "chardsp_oscillators.wav";

    if (! writeWav (path, buffer))
    {
        std::cerr << "Failed to write " << path << '\n';
        return 1;
    }

    std::cout << "Wrote " << path << "\nSegments:\n";
    for (size_t i = 0; i < labels.size(); ++i)
        std::cout << "  " << i + 1 << ". " << labels[i] << '\n';
}
