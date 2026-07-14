#include <algorithm>
#include <iomanip>
#include <iostream>
#include <limits>

#include "chardsp/chardsp_EllipticSawOscillator.h"
#include "chardsp/chardsp_SmoothedValue.h"
#include "choc/audio/choc_SampleBuffers.h"

int main()
{
    constexpr choc::buffer::ChannelCount channels = 2;
    constexpr choc::buffer::FrameCount frames = 128;
    constexpr float sampleRate = 48000.0f;

    choc::buffer::ChannelArrayBuffer<float> buffer (channels, frames);
    auto view = buffer.getView();
    chardsp::EllipticSawOscillator oscillator;
    chardsp::SmoothedValue gain;
    oscillator.prepare (sampleRate);
    oscillator.setFrequency (440.0f);
    gain.reset (0.0f);
    gain.setTarget (0.5f, frames);

    for (choc::buffer::FrameCount frame = 0; frame < frames; ++frame)
    {
        const auto sample = oscillator.next() * gain.next();

        for (choc::buffer::ChannelCount channel = 0; channel < channels; ++channel)
            view.getSample (channel, frame) = sample;
    }

    float minimum = std::numeric_limits<float>::infinity();
    float maximum = -std::numeric_limits<float>::infinity();
    double sum = 0.0;

    for (choc::buffer::FrameCount frame = 0; frame < frames; ++frame)
    {
        const auto sample = view.getSample (0, frame);
        minimum = std::min (minimum, sample);
        maximum = std::max (maximum, sample);
        sum += sample;
    }

    std::cout << std::fixed << std::setprecision (9)
              << "frames=" << frames
              << " first=" << view.getSample (0, 0)
              << " middle=" << view.getSample (0, frames / 2)
              << " last=" << view.getSample (0, frames - 1)
              << " min=" << minimum
              << " max=" << maximum
              << " sum=" << sum << '\n';
}
