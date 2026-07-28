#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>

#include "chardsp/chardsp_DelayLine.h"
#include "chardsp/chardsp_Oscillator.h"
#include "choc/audio/choc_AudioFileFormat_WAV.h"
#include "choc/audio/choc_SampleBuffers.h"

int main (int argc, const char* const* argv)
{
    constexpr float sampleRate = 48000.0f;
    constexpr uint32_t frames = 2 * 48000;
    const std::filesystem::path path = argc > 1
        ? argv[1] : "chardsp_delay_line.wav";
    chardsp::SineOscillator<float> oscillator;
    oscillator.prepare (sampleRate);
    oscillator.setFrequency (220.0f);
    chardsp::DelayLine<float, 48000> delay;
    delay.reset();
    delay.setDelay (sampleRate * 0.25f);
    choc::buffer::ChannelArrayBuffer<float> buffer (2, frames);

    for (uint32_t frame = 0; frame < frames; ++frame)
    {
        const auto dry = frame < 4800 ? oscillator.next() : 0.0f;
        const auto delayed = delay.read();
        delay.write (dry + delayed * 0.55f);
        const auto sample = (dry + delayed * 0.7f) * 0.2f;

        for (uint32_t channel = 0; channel < 2; ++channel)
            buffer.getSample (channel, frame) = sample;
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
              << " using separate feedback read/write operations\n";
}
