#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <type_traits>

#include "chardsp/chardsp_ADSREnvelope.h"
#include "chardsp/chardsp_ColoredNoise.h"
#include "chardsp/chardsp_Downsampler4x.h"
#include "chardsp/chardsp_EllipticSawOscillator.h"
#include "chardsp/chardsp_SmoothedValue.h"
#include "chardsp/chardsp_StateVariableFilter.h"
#include "choc/audio/choc_Oscillators.h"

namespace
{

std::atomic<size_t> allocationCount { 0 };

struct TestContext
{
    void check (bool condition, const char* expression, int line)
    {
        if (! condition)
        {
            ++failures;
            std::cerr << "line " << line << ": " << expression << '\n';
        }
    }

    int failures = 0;
};

#define CHECK(expression) context.check ((expression), #expression, __LINE__)

bool near (float actual, float expected, float tolerance = 0.000001f)
{
    return std::abs (actual - expected) <= tolerance;
}

void testSmoothedValue (TestContext& context)
{
    chardsp::SmoothedValue value;
    CHECK (near (value.getCurrent(), 0.0f));
    CHECK (near (value.next(), 0.0f));

    value.reset (-2.0f);
    CHECK (near (value.getCurrent(), -2.0f));
    CHECK (near (value.next(), -2.0f));

    value.setTarget (3.0f, 0);
    CHECK (near (value.getCurrent(), 3.0f));
    CHECK (near (value.next(), 3.0f));

    value.setTarget (-1.0f, 1);
    CHECK (near (value.next(), -1.0f));
    CHECK (near (value.next(), -1.0f));

    value.reset (-1.0f);
    value.setTarget (1.0f, 4);
    CHECK (near (value.next(), -0.5f));
    CHECK (near (value.next(), 0.0f));
    CHECK (near (value.next(), 0.5f));
    CHECK (near (value.next(), 1.0f));
    CHECK (near (value.getCurrent(), 1.0f));
    CHECK (near (value.next(), 1.0f));

    value.reset (0.0f);
    value.setTarget (1.0f, 4);
    CHECK (near (value.next(), 0.25f));
    CHECK (near (value.next(), 0.5f));
    value.setTarget (-0.5f, 2);
    CHECK (near (value.next(), 0.0f));
    CHECK (near (value.next(), -0.5f));
    CHECK (near (value.next(), -0.5f));
}

void testEllipticSaw (TestContext& context)
{
    chardsp::EllipticSawOscillator unprepared;
    unprepared.setFrequency (440.0f);
    CHECK (near (unprepared.next(), 0.0f));

    chardsp::EllipticSawOscillator oscillator;
    oscillator.prepare (48000.0f);
    oscillator.setFrequency (0.0f);

    for (int i = 0; i < 128; ++i)
        CHECK (near (oscillator.next(), 0.0f));

    oscillator.setFrequency (440.0f);
    float minimum = std::numeric_limits<float>::infinity();
    float maximum = -std::numeric_limits<float>::infinity();
    float previous = oscillator.next();
    float maximumDelta = 0.0f;
    CHECK (std::isfinite (previous));

    const auto allocationsBeforeRender = allocationCount.load();

    for (int i = 1; i < 200000; ++i)
    {
        const auto sample = oscillator.next();
        CHECK (std::isfinite (sample));
        minimum = std::min (minimum, sample);
        maximum = std::max (maximum, sample);
        maximumDelta = std::max (maximumDelta, std::abs (sample - previous));
        previous = sample;
    }

    CHECK (allocationCount.load() == allocationsBeforeRender);
    CHECK (maximum - minimum > 1.0f);
    CHECK (maximumDelta < 2.5f);

    oscillator.setFrequency (0.0f);
    CHECK (near (oscillator.next(), 0.0f));
    CHECK (near (oscillator.next(), 0.0f));

    chardsp::EllipticSawOscillator limited;
    chardsp::EllipticSawOscillator explicitLimit;
    limited.prepare (48000.0f);
    explicitLimit.prepare (48000.0f);
    limited.setFrequency (std::numeric_limits<float>::infinity());
    explicitLimit.setFrequency (48000.0f * 0.49f);

    for (int i = 0; i < 4096; ++i)
    {
        const auto limitedSample = limited.next();
        const auto explicitSample = explicitLimit.next();
        CHECK (std::isfinite (limitedSample));
        CHECK (near (limitedSample, explicitSample));
    }

    chardsp::EllipticSawOscillator oneRender;
    chardsp::EllipticSawOscillator splitRender;
    oneRender.prepare (44100.0f);
    splitRender.prepare (44100.0f);
    oneRender.setFrequency (997.0f);
    splitRender.setFrequency (997.0f);
    std::array<float, 4096> expected {};

    for (auto& sample : expected)
        sample = oneRender.next();

    for (size_t blockStart = 0; blockStart < expected.size(); blockStart += 127)
        for (size_t i = blockStart;
             i < std::min (blockStart + 127, expected.size()); ++i)
            CHECK (near (splitRender.next(), expected[i]));
}

void testChocTriangleModulation (TestContext& context)
{
    choc::oscillator::Triangle<float> triangle;
    triangle.resetPhase();
    triangle.setFrequency (1.0f, 8.0f);

    std::array<float, 8> bipolar {};
    std::array<float, 8> unipolar {};

    for (size_t i = 0; i < bipolar.size(); ++i)
    {
        bipolar[i] = triangle.getSample();
        unipolar[i] = (bipolar[i] + 1.0f) * 0.5f;
        CHECK (std::isfinite (bipolar[i]));
        CHECK (bipolar[i] >= -1.0f && bipolar[i] <= 1.0f);
        CHECK (unipolar[i] >= 0.0f && unipolar[i] <= 1.0f);
    }

    CHECK (near (bipolar[0], 1.0f));
    CHECK (near (bipolar[2], 0.0f));
    CHECK (near (bipolar[3], -0.5f));
    CHECK (near (bipolar[7], 1.0f));
    CHECK (near (unipolar[0], 1.0f));
    CHECK (near (unipolar[3], 0.25f));
    CHECK (near (triangle.getSample(), bipolar[0]));

    choc::oscillator::Triangle<float> twiceAsFast;
    twiceAsFast.resetPhase();
    twiceAsFast.setFrequency (2.0f, 8.0f);
    std::array<float, 8> fast {};

    for (auto& sample : fast)
        sample = twiceAsFast.getSample();

    for (size_t i = 0; i < 4; ++i)
        CHECK (near (fast[i], fast[i + 4]));
}

void testADSREnvelopeParity (TestContext& context)
{
    constexpr std::array<float, 12> exponentialReference {
        0.5758192539f, 0.8333333731f, 0.9484972358f, 1.000000119f,
        1.0f, 0.5794430375f, 0.3836265802f, 0.2924520075f,
        0.1296163648f, 0.05300405994f, 0.01695885509f, 0.0f
    };
    constexpr std::array<float, 12> linearReference {
        0.25f, 0.5f, 0.75f, 1.0f, 0.8125f, 0.625f,
        0.4375f, 0.25f, 0.1875f, 0.125f, 0.0625f, 0.0f
    };

    const auto checkShape = [&] (chardsp::EnvelopeShape shape,
                                  const std::array<float, 12>& reference)
    {
        chardsp::ADSREnvelope envelope;
        envelope.prepare (8.0f);
        envelope.setAttack (0.5f);
        envelope.setDecay (0.5f);
        envelope.setSustain (0.25f);
        envelope.setRelease (0.5f);
        envelope.setShape (shape);
        envelope.trigger();

        for (size_t i = 0; i < reference.size(); ++i)
        {
            if (i == 8)
                envelope.release();

            CHECK (near (envelope.next(), reference[i], 0.0000002f));
        }

        CHECK (near (envelope.next(), 0.0f));
        CHECK (! envelope.isActive());
        CHECK (near (envelope.getCurrent(), 0.0f));
    };

    checkShape (chardsp::EnvelopeShape::exponential, exponentialReference);
    checkShape (chardsp::EnvelopeShape::linear, linearReference);
}

void testADSREnvelopeBehavior (TestContext& context)
{
    chardsp::ADSREnvelope envelope;
    envelope.prepare (10.0f);
    envelope.setShape (chardsp::EnvelopeShape::linear);
    envelope.setAttack (0.3f);
    envelope.setDecay (0.2f);
    envelope.setSustain (0.4f);
    envelope.setRelease (0.2f);
    envelope.trigger();
    CHECK (envelope.isActive());
    CHECK (near (envelope.next(), 1.0f / 3.0f));
    CHECK (near (envelope.next(), 2.0f / 3.0f));
    CHECK (near (envelope.next(), 1.0f));
    CHECK (near (envelope.next(), 0.7f));
    CHECK (near (envelope.next(), 0.4f));

    for (int i = 0; i < 8; ++i)
        CHECK (near (envelope.next(), 0.4f));

    envelope.release();
    CHECK (near (envelope.next(), 0.2f));
    CHECK (near (envelope.next(), 0.0f));
    CHECK (! envelope.isActive());

    envelope.setAttack (0.0f);
    envelope.setDecay (0.0f);
    envelope.setSustain (0.3f);
    envelope.setRelease (0.0f);
    envelope.trigger();
    CHECK (envelope.isActive());
    CHECK (near (envelope.getCurrent(), 0.3f));
    CHECK (near (envelope.next(), 0.3f));
    envelope.release();
    CHECK (near (envelope.getCurrent(), 0.0f));
    CHECK (! envelope.isActive());

    envelope.reset();
    envelope.setAttack (0.4f);
    envelope.setDecay (0.0f);
    envelope.setSustain (0.25f);
    envelope.retrigger();
    CHECK (near (envelope.next(), 0.25f));

    envelope.reset();
    envelope.setAttack (0.4f);
    envelope.setDecay (0.0f);
    envelope.setSustain (0.4f);
    envelope.trigger();
    envelope.next();
    envelope.next();
    const auto beforeRetrigger = envelope.getCurrent();
    envelope.retrigger();
    CHECK (near (envelope.getCurrent(), beforeRetrigger));
    CHECK (envelope.next() >= beforeRetrigger);

    envelope.setAttack (0.0f);
    envelope.setDecay (0.0f);
    envelope.setSustain (0.25f);
    envelope.trigger();
    envelope.forceRelease (0.4f);
    CHECK (near (envelope.next(), 0.1875f));
    CHECK (near (envelope.next(), 0.125f));
    CHECK (near (envelope.next(), 0.0625f));
    CHECK (near (envelope.next(), 0.0f));
    CHECK (! envelope.isActive());

    envelope.trigger();
    envelope.forceRelease (0.0f);
    CHECK (near (envelope.getCurrent(), 0.0f));
    CHECK (! envelope.isActive());
}

void testADSREnvelopeLiveUpdates (TestContext& context)
{
    chardsp::ADSREnvelope envelope;
    envelope.prepare (10.0f);
    envelope.setShape (chardsp::EnvelopeShape::linear);
    envelope.setAttack (1.0f);
    envelope.setDecay (1.0f);
    envelope.setSustain (0.2f);
    envelope.setRelease (1.0f);
    envelope.trigger();
    CHECK (near (envelope.next(), 0.1f));
    CHECK (near (envelope.next(), 0.2f));

    envelope.setAttack (0.2f);
    CHECK (near (envelope.getCurrent(), 0.2f));
    CHECK (near (envelope.next(), 0.6f));
    CHECK (near (envelope.next(), 1.0f));
    CHECK (near (envelope.next(), 0.92f));

    envelope.setSustain (0.8f);
    envelope.setDecay (0.2f);
    CHECK (near (envelope.getCurrent(), 0.92f));
    CHECK (near (envelope.next(), 0.86f));
    CHECK (near (envelope.next(), 0.8f));

    envelope.release();
    CHECK (near (envelope.next(), 0.72f));
    envelope.setRelease (0.2f);
    CHECK (near (envelope.getCurrent(), 0.72f));
    CHECK (near (envelope.next(), 0.36f));
    CHECK (near (envelope.next(), 0.0f));

    envelope.setAttack (1.0f);
    envelope.setDecay (0.0f);
    envelope.setSustain (1.0f);
    envelope.trigger();
    envelope.next();
    const auto beforeShapeChange = envelope.getCurrent();
    envelope.setShape (chardsp::EnvelopeShape::exponential);
    CHECK (near (envelope.getCurrent(), beforeShapeChange));
    CHECK (std::isfinite (envelope.next()));

    const auto allocationsBeforeRender = allocationCount.load();
    bool allFinite = true;

    for (int i = 0; i < 200000; ++i)
    {
        if (i % 10000 == 0)
            envelope.retrigger();
        if (i % 10000 == 8000)
            envelope.release();
        allFinite = allFinite && std::isfinite (envelope.next());
    }

    CHECK (allFinite);
    CHECK (allocationCount.load() == allocationsBeforeRender);
}

void testStateVariableFilterParity (TestContext& context)
{
    constexpr std::array<float, 12> lowpassReference {
        0.0638653487f, 0.2221318334f, 0.3453536034f, 0.3475232124f,
        0.2603360713f, 0.1298707128f, 0.0009858225239f, -0.09381454438f,
        -0.1393930465f, -0.1372193992f, -0.1005705744f, -0.04799349979f
    };
    constexpr std::array<float, 12> highpassReference {
        0.8029291034f, -0.419026643f, -0.4054655433f, -0.291936487f,
        -0.1340743452f, 0.01598238759f, 0.1219794974f, 0.1685767174f,
        0.1596950889f, 0.1123836935f, 0.04896689206f, -0.01006412879f
    };

    const auto checkMode = [&] (chardsp::FilterMode mode,
                                 const std::array<float, 12>& reference)
    {
        chardsp::StateVariableFilter filter;
        filter.prepare (48000.0f);
        filter.setMode (mode);
        filter.setCutoffAndQ (4200.0f, 1.7f);

        for (size_t i = 0; i < reference.size(); ++i)
            CHECK (near (filter.process (i == 0 ? 1.0f : 0.0f), reference[i]));
    };

    checkMode (chardsp::FilterMode::lowpass, lowpassReference);
    checkMode (chardsp::FilterMode::highpass, highpassReference);
}

float filterSineRMS (float sampleRate,
                     float cutoff,
                     float frequency,
                     chardsp::FilterMode mode)
{
    chardsp::StateVariableFilter filter;
    filter.prepare (sampleRate);
    filter.setMode (mode);
    filter.setCutoffAndQ (cutoff, 0.70710678f);
    constexpr int discardSamples = 4096;
    constexpr int measuredSamples = 8192;
    double sumSquares = 0.0;

    for (int i = 0; i < discardSamples + measuredSamples; ++i)
    {
        const auto input = std::sin (6.283185307179586f * frequency
                                     * static_cast<float> (i) / sampleRate);
        const auto output = filter.process (input);

        if (i >= discardSamples)
            sumSquares += static_cast<double> (output) * output;
    }

    return static_cast<float> (std::sqrt (sumSquares / measuredSamples));
}

void testStateVariableFilterBehavior (TestContext& context)
{
    for (const auto sampleRate : { 44100.0f, 48000.0f, 96000.0f })
    {
        const auto lowpassLow = filterSineRMS (sampleRate, 1000.0f, 200.0f,
                                               chardsp::FilterMode::lowpass);
        const auto lowpassHigh = filterSineRMS (sampleRate, 1000.0f, 12000.0f,
                                                chardsp::FilterMode::lowpass);
        const auto highpassLow = filterSineRMS (sampleRate, 1000.0f, 200.0f,
                                                chardsp::FilterMode::highpass);
        const auto highpassHigh = filterSineRMS (sampleRate, 1000.0f, 12000.0f,
                                                 chardsp::FilterMode::highpass);
        CHECK (lowpassLow > 0.65f);
        CHECK (lowpassHigh < 0.02f);
        CHECK (highpassLow < 0.04f);
        CHECK (highpassHigh > 0.65f);
    }

    chardsp::StateVariableFilter first;
    chardsp::StateVariableFilter second;
    first.prepare (48000.0f);
    second.prepare (48000.0f);
    first.setMode (chardsp::FilterMode::lowpass);
    second.setMode (chardsp::FilterMode::highpass);
    first.setCutoffAndQ (3000.0f, 0.9f);
    second.setCutoffAndQ (3000.0f, 0.9f);

    for (int i = 0; i < 128; ++i)
    {
        const auto input = std::sin (0.17f * static_cast<float> (i));
        first.process (input);
        second.process (input);
    }

    first.setMode (chardsp::FilterMode::highpass);
    CHECK (near (first.process (0.25f), second.process (0.25f)));

    first.reset();
    second.reset();
    CHECK (near (first.process (1.0f), second.process (1.0f)));

    chardsp::StateVariableFilter clamped;
    chardsp::StateVariableFilter explicitLimits;
    clamped.prepare (48000.0f);
    explicitLimits.prepare (48000.0f);
    clamped.setCutoffAndQ (-100.0f, -5.0f);
    explicitLimits.setCutoffAndQ (0.0f, 0.000001f);
    CHECK (near (clamped.process (1.0f), explicitLimits.process (1.0f)));

    clamped.reset();
    explicitLimits.reset();
    clamped.setCutoffAndQ (1000000.0f, 0.7f);
    explicitLimits.setCutoffAndQ (48000.0f * 0.48f, 0.7f);

    for (int i = 0; i < 128; ++i)
        CHECK (near (clamped.process (i == 0 ? 1.0f : 0.0f),
                     explicitLimits.process (i == 0 ? 1.0f : 0.0f)));

    chardsp::StateVariableFilter silence;
    silence.prepare (96000.0f);
    silence.setCutoffAndQ (20000.0f, 0.5f);
    bool stableSilence = true;

    for (int i = 0; i < 100000; ++i)
        stableSilence = stableSilence && silence.process (0.0f) == 0.0f;

    CHECK (stableSilence);

    const auto allocationsBeforeRender = allocationCount.load();
    bool allFinite = true;

    for (int i = 0; i < 200000; ++i)
    {
        const auto input = std::sin (0.013f * static_cast<float> (i));
        allFinite = allFinite && std::isfinite (silence.process (input));
    }

    CHECK (allFinite);
    CHECK (allocationCount.load() == allocationsBeforeRender);
}

void testColoredNoiseParity (TestContext& context)
{
    constexpr std::array<std::array<float, 8>, 4> references {{
        {{ 0.01566551067f, 0.008858673275f, 0.01390580833f, 0.02395696566f,
           0.007478474639f, 0.003550637746f, 0.0006154062576f, -0.001146828406f }},
        {{ 0.3141725659f, 0.05324643478f, 0.1570387632f, 0.3107728958f,
           -0.1209541485f, -0.1147533357f, -0.1155554503f, -0.09962517023f }},
        {{ 0.9161118865f, -0.396961391f, 0.2957757413f, 0.5887628794f,
           -0.9619733691f, -0.2291732728f, -0.1714017987f, -0.103011474f }},
        {{ 0.8703062534f, -1.0f, 0.6581002474f, 0.2783377767f,
           -1.0f, 0.6961601377f, 0.05488289893f, 0.06497080624f }}
    }};
    constexpr std::array<chardsp::NoiseColor, 4> colors {
        chardsp::NoiseColor::brown,
        chardsp::NoiseColor::pink,
        chardsp::NoiseColor::white,
        chardsp::NoiseColor::blue
    };

    for (size_t color = 0; color < colors.size(); ++color)
    {
        chardsp::ColoredNoise noise;
        noise.reset (0x12345678u);
        noise.setColor (colors[color]);

        for (const auto reference : references[color])
            CHECK (near (noise.next(), reference));
    }
}

void testColoredNoiseBehavior (TestContext& context)
{
    chardsp::ColoredNoise noise;
    noise.setColor (chardsp::NoiseColor::pink);
    noise.reset (42);
    std::array<float, 64> firstRender {};

    for (auto& sample : firstRender)
        sample = noise.next();

    noise.reset (42);

    for (const auto expected : firstRender)
        CHECK (near (noise.next(), expected));

    std::array<chardsp::ColoredNoise, 4> fixedColors;
    constexpr std::array<chardsp::NoiseColor, 4> colors {
        chardsp::NoiseColor::brown,
        chardsp::NoiseColor::pink,
        chardsp::NoiseColor::white,
        chardsp::NoiseColor::blue
    };

    for (size_t i = 0; i < fixedColors.size(); ++i)
    {
        fixedColors[i].reset (99);
        fixedColors[i].setColor (colors[i]);
    }

    chardsp::ColoredNoise switching;
    switching.reset (99);

    for (int i = 0; i < 256; ++i)
    {
        std::array<float, 4> endpoints {};

        for (size_t color = 0; color < endpoints.size(); ++color)
            endpoints[color] = fixedColors[color].next();

        const auto selected = static_cast<size_t> (i % 4);
        switching.setColor (colors[selected]);
        CHECK (near (switching.next(), endpoints[selected]));
    }

    const auto allocationsBeforeRender = allocationCount.load();

    for (const auto color : colors)
    {
        noise.reset (0x87654321u);
        noise.setColor (color);
        double sum = 0.0;
        double sumSquares = 0.0;
        bool finiteAndBounded = true;
        constexpr int sampleCount = 200000;

        for (int i = 0; i < sampleCount; ++i)
        {
            const auto sample = noise.next();
            finiteAndBounded = finiteAndBounded && std::isfinite (sample)
                               && sample >= -1.0f && sample <= 1.0f;
            sum += sample;
            sumSquares += static_cast<double> (sample) * sample;
        }

        const auto mean = static_cast<float> (sum / sampleCount);
        const auto rms = static_cast<float> (std::sqrt (sumSquares / sampleCount));
        CHECK (finiteAndBounded);
        CHECK (std::abs (mean) < 0.15f);
        CHECK (rms > 0.01f && rms < 0.9f);
    }

    CHECK (allocationCount.load() == allocationsBeforeRender);
}

void testDownsamplerParity (TestContext& context)
{
    constexpr std::array<float, 12> reference {
        0.002049217699f, 0.04932555556f, 0.1835883409f, 0.07104226947f,
        -0.1017052084f, 0.07502372563f, -0.04592199996f, 0.0247429572f,
        -0.01101632882f, 0.002590026706f, 0.002369836438f, -0.005135977641f
    };
    chardsp::Downsampler4x downsampler;

    for (size_t i = 0; i < reference.size(); ++i)
        CHECK (near (downsampler.process (i == 0 ? 1.0f : 0.0f,
                                          0.0f,
                                          0.0f,
                                          0.0f),
                     reference[i]));
}

float downsampledSineRMS (float frequency)
{
    constexpr float oversampledRate = 192000.0f;
    constexpr int discardedOutputs = 4096;
    constexpr int measuredOutputs = 8192;
    chardsp::Downsampler4x downsampler;
    double sumSquares = 0.0;

    for (int outputIndex = 0; outputIndex < discardedOutputs + measuredOutputs;
         ++outputIndex)
    {
        std::array<float, 4> samples {};

        for (int oversample = 0; oversample < 4; ++oversample)
        {
            const auto sampleIndex = outputIndex * 4 + oversample;
            samples[static_cast<size_t> (oversample)] =
                std::sin (6.283185307179586f * frequency
                          * static_cast<float> (sampleIndex) / oversampledRate);
        }

        const auto output = downsampler.process (samples);

        if (outputIndex >= discardedOutputs)
            sumSquares += static_cast<double> (output) * output;
    }

    return static_cast<float> (std::sqrt (sumSquares / measuredOutputs));
}

void testDownsamplerBehavior (TestContext& context)
{
    chardsp::Downsampler4x scalar;
    chardsp::Downsampler4x array;

    for (int i = 0; i < 1024; ++i)
    {
        const std::array<float, 4> samples {
            std::sin (0.07f * static_cast<float> (i * 4)),
            std::sin (0.07f * static_cast<float> (i * 4 + 1)),
            std::sin (0.07f * static_cast<float> (i * 4 + 2)),
            std::sin (0.07f * static_cast<float> (i * 4 + 3))
        };
        CHECK (near (scalar.process (samples[0], samples[1], samples[2], samples[3]),
                     array.process (samples)));
    }

    scalar.reset();
    array.reset();

    for (int i = 0; i < 128; ++i)
    {
        const auto expected = array.process (i == 0
                                                 ? std::array<float, 4> { 1.0f, 0.0f,
                                                                          0.0f, 0.0f }
                                                 : std::array<float, 4> {});
        const auto actual = scalar.process (i == 0 ? 1.0f : 0.0f,
                                            0.0f,
                                            0.0f,
                                            0.0f);
        CHECK (near (actual, expected));
    }

    scalar.reset();
    float dc = 0.0f;

    for (int i = 0; i < 4096; ++i)
        dc = scalar.process (1.0f, 1.0f, 1.0f, 1.0f);

    CHECK (near (dc, 1.0f, 0.00001f));

    const auto lowFrequencyRMS = downsampledSineRMS (1000.0f);
    const auto highFrequencyRMS = downsampledSineRMS (40000.0f);
    CHECK (lowFrequencyRMS > 0.65f);
    CHECK (highFrequencyRMS < lowFrequencyRMS * 0.1f);

    const auto allocationsBeforeRender = allocationCount.load();
    bool allFinite = true;

    for (int i = 0; i < 200000; ++i)
    {
        const auto input = std::sin (0.031f * static_cast<float> (i));
        allFinite = allFinite && std::isfinite (scalar.process (input,
                                                                -input,
                                                                input * 0.5f,
                                                                -input * 0.5f));
    }

    CHECK (allFinite);
    CHECK (allocationCount.load() == allocationsBeforeRender);
}

static_assert (std::is_nothrow_default_constructible_v<chardsp::ADSREnvelope>);
static_assert (std::is_nothrow_default_constructible_v<chardsp::StateVariableFilter>);
static_assert (std::is_nothrow_default_constructible_v<chardsp::ColoredNoise>);
static_assert (std::is_nothrow_default_constructible_v<chardsp::Downsampler4x>);

} // namespace

void* operator new (std::size_t size)
{
    allocationCount.fetch_add (1, std::memory_order_relaxed);

    if (auto* memory = std::malloc (size))
        return memory;

    throw std::bad_alloc {};
}

void* operator new[] (std::size_t size)
{
    return ::operator new (size);
}

void operator delete (void* memory) noexcept { std::free (memory); }
void operator delete[] (void* memory) noexcept { std::free (memory); }
void operator delete (void* memory, std::size_t) noexcept { std::free (memory); }
void operator delete[] (void* memory, std::size_t) noexcept { std::free (memory); }

int main()
{
    TestContext context;
    testSmoothedValue (context);
    testEllipticSaw (context);
    testChocTriangleModulation (context);
    testADSREnvelopeParity (context);
    testADSREnvelopeBehavior (context);
    testADSREnvelopeLiveUpdates (context);
    testStateVariableFilterParity (context);
    testStateVariableFilterBehavior (context);
    testColoredNoiseParity (context);
    testColoredNoiseBehavior (context);
    testDownsamplerParity (context);
    testDownsamplerBehavior (context);

    if (context.failures == 0)
    {
        std::cout << "chardsp tests: all checks passed\n";
        return 0;
    }

    std::cerr << "chardsp tests: " << context.failures << " checks failed\n";
    return 1;
}
