#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "chardsp/chardsp_ADSREnvelope.h"
#include "chardsp/chardsp_Oscillator.h"
#include "choc/audio/choc_AudioFileFormat_WAV.h"
#include "choc/audio/choc_SampleBuffers.h"

int main (int argc, const char* const* argv)
{
    constexpr float sampleRate = 48000.0f;
    constexpr uint32_t frames = 2 * 48000;
    const std::filesystem::path path = argc > 1
        ? argv[1] : "chardsp_adsr.wav";
    const auto velocity = argc > 2
        ? std::clamp (std::strtof (argv[2], nullptr), 0.0f, 1.0f) : 0.7f;

    chardsp::SineOscillator<float> oscillator;
    oscillator.prepare (sampleRate);
    oscillator.setFrequency (220.0f);

    chardsp::ADSREnvelope<float> envelope;
    envelope.prepare (sampleRate);
    envelope.setShape (chardsp::EnvelopeShape::exponential);
    envelope.setAttack (0.15f);
    envelope.setDecay (0.25f);
    envelope.setSustain (0.45f);
    envelope.setRelease (0.55f);
    envelope.trigger();

    choc::buffer::ChannelArrayBuffer<float> buffer (2, frames);
    auto output = buffer.getView();

    for (uint32_t frame = 0; frame < frames; ++frame)
    {
        if (frame == 48000)
            envelope.release();

        // ADSR remains normalized. Velocity scales its output level in the
        // voice, exactly as a velocity-scaled envelope/VCA patch would.
        const auto envelopeLevel = velocity * envelope.next();
        const auto sample = 0.25f * oscillator.next() * envelopeLevel;

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

    std::cout << "Wrote " << path << " with velocity " << velocity << '\n';
}
