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
#include "chardsp/chardsp_BasicSynthVoice.h"
#include "chardsp/chardsp_ColoredNoise.h"
#include "chardsp/chardsp_Downsampler4x.h"
#include "chardsp/chardsp_EllipticSawOscillator.h"
#include "chardsp/chardsp_MonoVoiceController.h"
#include "chardsp/chardsp_Oscillator.h"
#include "chardsp/chardsp_Portamento.h"
#include "chardsp/chardsp_SmoothedValue.h"
#include "chardsp/chardsp_StateVariableFilter.h"
#include "chardsp/chardsp_VoicePool.h"
#include "choc/audio/choc_Oscillators.h"
#include "choc/platform/choc_UnitTest.h"

namespace
{

std::atomic<size_t> allocationCount { 0 };

#define CHECK(expression) CHOC_EXPECT_TRUE (expression)

bool near (float actual, float expected, float tolerance = 0.000001f)
{
    return std::abs (actual - expected) <= tolerance;
}

void testSmoothedValue (choc::test::TestProgress& progress)
{
    chardsp::SmoothedValue<float> value;
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

void testEllipticSaw (choc::test::TestProgress& progress)
{
    chardsp::EllipticSawOscillator<float> unprepared;
    unprepared.setFrequency (440.0f);
    CHECK (near (unprepared.next(), 0.0f));

    chardsp::EllipticSawOscillator<float> oscillator;
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

    chardsp::EllipticSawOscillator<float> limited;
    chardsp::EllipticSawOscillator<float> explicitLimit;
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

    chardsp::EllipticSawOscillator<float> oneRender;
    chardsp::EllipticSawOscillator<float> splitRender;
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

void testOscillator (choc::test::TestProgress& progress)
{
    constexpr std::array waveforms {
        chardsp::OscillatorWaveform::saw,
        chardsp::OscillatorWaveform::ramp,
        chardsp::OscillatorWaveform::pulse,
        chardsp::OscillatorWaveform::triangle,
        chardsp::OscillatorWaveform::sine
    };
    constexpr std::array<std::array<float, 6>, 5> reference {{
        {{ 0.002756481f, 0.095859312f, 0.598600566f,
           1.067782998f, 0.649674058f, 0.579240561f }},
        {{ -0.002756481f, -0.095859312f, -0.598600566f,
           -1.067782998f, -0.649674058f, -0.579240561f }},
        {{ 0.002501482f, 0.054757807f, 0.042682324f,
           -0.799973488f, -1.350166559f, -0.746327102f }},
        {{ -0.002728588f, -0.093261167f, -0.564077377f,
           -0.920363069f, -0.357993066f, -0.175391957f }},
        {{ 0.000087309f, 0.008079876f, 0.105925441f,
           0.438298821f, 0.801043510f, 0.942837477f }}
    }};

    chardsp::Oscillator<float> unprepared;
    unprepared.setFrequency (440.0f);
    CHECK (near (unprepared.next(), 0.0f));

    for (size_t waveform = 0; waveform < waveforms.size(); ++waveform)
    {
        chardsp::Oscillator<float> oscillator;
        oscillator.prepare (48000.0f);
        oscillator.setWaveform (waveforms[waveform]);
        oscillator.setShape (0.0f);
        oscillator.setFrequency (3000.0f);
        oscillator.reset();

        for (const auto expected : reference[waveform])
            CHECK (near (oscillator.next(), expected, 0.000002f));
    }

    chardsp::Oscillator<float> first;
    chardsp::Oscillator<float> second;
    first.prepare (44100.0f);
    second.prepare (44100.0f);
    first.setWaveform (chardsp::OscillatorWaveform::triangle);
    second.setWaveform (chardsp::OscillatorWaveform::triangle);
    first.setShape (0.75f);
    second.setShape (0.75f);
    first.setFrequency (997.0f);
    second.setFrequency (997.0f);
    first.reset (0.37f);
    second.reset (0.37f);
    const auto allocationsBeforeRender = allocationCount.load();

    for (int i = 0; i < 200000; ++i)
    {
        const auto sample = first.next();
        CHECK (std::isfinite (sample));
        CHECK (near (sample, second.next()));
    }

    CHECK (allocationCount.load() == allocationsBeforeRender);

    first.setShape (-1.0f);
    CHECK (near (first.getShape(), 0.0f));
    first.setShape (2.0f);
    CHECK (near (first.getShape(), 1.0f));
    first.setFrequency (std::numeric_limits<float>::infinity());
    CHECK (near (first.getFrequency(), 44100.0f * 0.49f));
    CHECK (std::isfinite (first.next()));

    // advance() follows the same phase timeline as discarding a rendered
    // output sample, but does not keep stale oversampling-filter state.
    chardsp::Oscillator<float> advanced;
    chardsp::Oscillator<float> rendered;
    advanced.prepare (48000.0f);
    rendered.prepare (48000.0f);
    advanced.setWaveform (chardsp::OscillatorWaveform::sine);
    rendered.setWaveform (chardsp::OscillatorWaveform::sine);
    advanced.setFrequency (997.0f);
    rendered.setFrequency (997.0f);
    advanced.reset (0.37f);
    rendered.reset (0.37f);

    constexpr int skippedSamples = 137;
    for (int i = 0; i < skippedSamples; ++i)
    {
        advanced.advance();
        rendered.next();
    }

    // A waveform round-trip resets only the rendered oscillator's stale
    // downsampler, leaving its reference phase untouched.
    rendered.setWaveform (chardsp::OscillatorWaveform::triangle);
    rendered.setWaveform (chardsp::OscillatorWaveform::sine);

    for (int i = 0; i < 64; ++i)
        CHECK (near (advanced.next(), rendered.next(), 0.000002f));
}

void testChocTriangleModulation (choc::test::TestProgress& progress)
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

void testADSREnvelopeParity (choc::test::TestProgress& progress)
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
        chardsp::ADSREnvelope<float> envelope;
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

void testADSREnvelopeBehavior (choc::test::TestProgress& progress)
{
    chardsp::ADSREnvelope<float> envelope;
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

void testADSREnvelopeLiveUpdates (choc::test::TestProgress& progress)
{
    chardsp::ADSREnvelope<float> envelope;
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

void testStateVariableFilterParity (choc::test::TestProgress& progress)
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
    constexpr std::array<float, 12> bandpassReference {
        0.226449429f, 0.334721135f, 0.102190307f, -0.094497406f,
        -0.214644892f, -0.247950276f, -0.209041018f, -0.127095711f,
        -0.034513498f, 0.042220652f, 0.087726228f, 0.098697950f
    };

    const auto checkMode = [&] (chardsp::FilterMode mode,
                                 const std::array<float, 12>& reference)
    {
        chardsp::StateVariableFilter<float> filter;
        filter.prepare (48000.0f);
        filter.setMode (mode);
        filter.setCutoffAndQ (4200.0f, 1.7f);

        for (size_t i = 0; i < reference.size(); ++i)
            CHECK (near (filter.process (i == 0 ? 1.0f : 0.0f), reference[i]));
    };

    checkMode (chardsp::FilterMode::lowpass, lowpassReference);
    checkMode (chardsp::FilterMode::bandpass, bandpassReference);
    checkMode (chardsp::FilterMode::highpass, highpassReference);
}

float filterSineRMS (float sampleRate,
                     float cutoff,
                     float frequency,
                     chardsp::FilterMode mode)
{
    chardsp::StateVariableFilter<float> filter;
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

void testStateVariableFilterBehavior (choc::test::TestProgress& progress)
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
        const auto bandpassLow = filterSineRMS (sampleRate, 1000.0f, 200.0f,
                                                chardsp::FilterMode::bandpass);
        const auto bandpassCenter = filterSineRMS (sampleRate, 1000.0f, 1000.0f,
                                                   chardsp::FilterMode::bandpass);
        const auto bandpassHigh = filterSineRMS (sampleRate, 1000.0f, 12000.0f,
                                                 chardsp::FilterMode::bandpass);
        CHECK (lowpassLow > 0.65f);
        CHECK (lowpassHigh < 0.02f);
        CHECK (highpassLow < 0.04f);
        CHECK (highpassHigh > 0.65f);
        CHECK (bandpassLow < 0.16f);
        CHECK (bandpassCenter > 0.48f);
        CHECK (bandpassHigh < 0.07f);
    }

    chardsp::StateVariableFilter<float> first;
    chardsp::StateVariableFilter<float> second;
    chardsp::StateVariableFilter<float> bandpass;
    first.prepare (48000.0f);
    second.prepare (48000.0f);
    bandpass.prepare (48000.0f);
    first.setMode (chardsp::FilterMode::lowpass);
    second.setMode (chardsp::FilterMode::highpass);
    bandpass.setMode (chardsp::FilterMode::bandpass);
    first.setCutoffAndQ (3000.0f, 0.9f);
    second.setCutoffAndQ (3000.0f, 0.9f);
    bandpass.setCutoffAndQ (3000.0f, 0.9f);

    for (int i = 0; i < 128; ++i)
    {
        const auto input = std::sin (0.17f * static_cast<float> (i));
        first.process (input);
        second.process (input);
        bandpass.process (input);
    }

    first.setMode (chardsp::FilterMode::highpass);
    CHECK (near (first.process (0.25f), second.process (0.25f)));

    first.reset();
    second.reset();
    CHECK (near (first.process (1.0f), second.process (1.0f)));

    first.reset();
    bandpass.reset();
    first.setMode (chardsp::FilterMode::bandpass);
    CHECK (near (first.process (1.0f), bandpass.process (1.0f)));

    chardsp::StateVariableFilter<float> clamped;
    chardsp::StateVariableFilter<float> explicitLimits;
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

    chardsp::StateVariableFilter<float> silence;
    silence.prepare (96000.0f);
    silence.setCutoffAndQ (20000.0f, 0.5f);
    bool stableSilence = true;

    for (int i = 0; i < 100000; ++i)
        stableSilence = stableSilence && silence.process (0.0f) == 0.0f;

    CHECK (stableSilence);

    chardsp::StateVariableFilter<float> decaying;
    decaying.prepare (48000.0f);
    decaying.setCutoffAndQ (1000.0f, 0.70710678f);
    CHECK (decaying.process (1.0f) != 0.0f);

    bool reachedExactZero = false;
    for (int i = 0; i < 100000; ++i)
    {
        if (decaying.process (0.0f) == 0.0f)
        {
            reachedExactZero = true;
            break;
        }
    }

    CHECK (reachedExactZero);
    CHECK (decaying.process (0.0f) == 0.0f);

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

void testColoredNoiseParity (choc::test::TestProgress& progress)
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
        chardsp::ColoredNoise<float> noise;
        noise.reset (0x12345678u);
        noise.setColor (colors[color]);

        for (const auto reference : references[color])
            CHECK (near (noise.next(), reference));
    }
}

void testColoredNoiseBehavior (choc::test::TestProgress& progress)
{
    chardsp::ColoredNoise<float> noise;
    noise.setColor (chardsp::NoiseColor::pink);
    noise.reset (42);
    std::array<float, 64> firstRender {};

    for (auto& sample : firstRender)
        sample = noise.next();

    noise.reset (42);

    for (const auto expected : firstRender)
        CHECK (near (noise.next(), expected));

    std::array<chardsp::ColoredNoise<float>, 4> fixedColors;
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

    chardsp::ColoredNoise<float> switching;
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

void testDownsamplerParity (choc::test::TestProgress& progress)
{
    constexpr std::array<float, 12> reference {
        0.002049217699f, 0.04932555556f, 0.1835883409f, 0.07104226947f,
        -0.1017052084f, 0.07502372563f, -0.04592199996f, 0.0247429572f,
        -0.01101632882f, 0.002590026706f, 0.002369836438f, -0.005135977641f
    };
    chardsp::Downsampler4x<float> downsampler;

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
    chardsp::Downsampler4x<float> downsampler;
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

void testDownsamplerBehavior (choc::test::TestProgress& progress)
{
    chardsp::Downsampler4x<float> scalar;
    chardsp::Downsampler4x<float> array;

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

void testDoublePrecisionTypes (choc::test::TestProgress& progress)
{
    static_assert (std::is_same_v<
        decltype (std::declval<chardsp::SmoothedValue<double>&>().next()),
        double>);
    static_assert (std::is_same_v<
        decltype (std::declval<chardsp::ADSREnvelope<double>&>().next()),
        double>);
    static_assert (std::is_same_v<
        decltype (std::declval<chardsp::Oscillator<double>&>().next()),
        double>);

    chardsp::SmoothedValue<double> smoother;
    smoother.reset (0.0);
    smoother.setTarget (1.0, 4);
    CHECK (std::abs (smoother.next() - 0.25) < 1.0e-12);

    chardsp::ADSREnvelope<double> envelope;
    envelope.prepare (48000.0);
    envelope.setAttack (0.001);
    envelope.setDecay (0.002);
    envelope.setSustain (0.4);
    envelope.setRelease (0.003);
    envelope.trigger();

    chardsp::StateVariableFilter<double> filter;
    filter.prepare (48000.0);
    filter.setMode (chardsp::FilterMode::bandpass);
    filter.setCutoffAndQ (2000.0, 0.8);

    chardsp::ColoredNoise<double> noise;
    noise.reset (0x12345678u);
    noise.setColor (chardsp::NoiseColor::pink);

    chardsp::Downsampler4x<double> downsampler;
    chardsp::Oscillator<double> oscillator;
    oscillator.prepare (48000.0);
    oscillator.setWaveform (chardsp::OscillatorWaveform::triangle);
    oscillator.setFrequency (440.0);

    chardsp::EllipticSawOscillator<double> ellipticSaw;
    ellipticSaw.prepare (48000.0);
    ellipticSaw.setFrequency (440.0);

    chardsp::BasicSynthVoice<double> basicVoice;
    basicVoice.prepare (48000.0, 3);
    basicVoice.start (69.0, 0.8);

    const auto allocationsBeforeRender = allocationCount.load();
    bool allFinite = true;

    for (int i = 0; i < 20000; ++i)
    {
        if (i == 10000)
            envelope.release();

        const auto excitation = oscillator.next() + noise.next() * 0.1;
        const auto filtered = filter.process (excitation) * envelope.next();
        const auto downsampled = downsampler.process (
            filtered, filtered * 0.5, -filtered * 0.25, filtered * 0.125);
        allFinite = allFinite && std::isfinite (downsampled)
                   && std::isfinite (ellipticSaw.next())
                   && std::isfinite (basicVoice.next());
    }

    CHECK (allFinite);
    CHECK (allocationCount.load() == allocationsBeforeRender);

    noise.reset (77);
    const auto expectedNoise = noise.next();
    noise.reset (77);
    CHECK (noise.next() == expectedNoise);
}

struct TestVoice
{
    void prepare (float newSampleRate, size_t newVoiceIndex) noexcept
    {
        sampleRate = newSampleRate;
        voiceIndex = newVoiceIndex;
        note = -1;
        startCount = 0;
        releaseCount = 0;
        forcedReleaseCount = 0;
        lastRenderedStartCount = 0;
        renderCount = 0;
        releaseSamplesRemaining = 0;
        active = false;
    }

    void start (int nextNote, float nextVelocity) noexcept
    {
        note = nextNote;
        velocity = nextVelocity;
        ++startCount;
        releaseSamplesRemaining = 0;
        active = true;
    }

    void release() noexcept
    {
        ++releaseCount;
        releaseSamplesRemaining = 4;
    }

    void forceRelease (float seconds) noexcept
    {
        ++forcedReleaseCount;
        lastForcedReleaseSeconds = seconds;
        releaseSamplesRemaining = std::max (1,
            static_cast<int> (seconds * sampleRate + 0.5f));
    }

    float next() noexcept
    {
        ++renderCount;
        lastRenderedStartCount = startCount;

        if (releaseSamplesRemaining > 0 && --releaseSamplesRemaining == 0)
            active = false;

        return active ? 0.1f : 0.0f;
    }

    bool isAvailable() const noexcept { return ! active; }

    float sampleRate = 0.0f;
    float velocity = 0.0f;
    float lastForcedReleaseSeconds = 0.0f;
    size_t voiceIndex = 0;
    int note = -1;
    int startCount = 0;
    int releaseCount = 0;
    int forcedReleaseCount = 0;
    int lastRenderedStartCount = 0;
    int renderCount = 0;
    int releaseSamplesRemaining = 0;
    bool active = false;
};

struct SourceKey
{
    uint64_t source = 0;
    float frequency = 0.0f;

    friend bool operator== (const SourceKey& a,
                            const SourceKey& b) noexcept
    {
        return a.source == b.source;
    }
};

struct SourceVoice : TestVoice
{
    void prepare (float newSampleRate, size_t newVoiceIndex) noexcept
    {
        TestVoice::prepare (newSampleRate, newVoiceIndex);
        source = 0;
        frequency = 0.0f;
    }

    void start (const SourceKey& key, float nextVelocity) noexcept
    {
        source = key.source;
        frequency = key.frequency;
        TestVoice::start (static_cast<int> (key.source), nextVelocity);
    }

    uint64_t source = 0;
    float frequency = 0.0f;
};

void testVoicePool (choc::test::TestProgress& progress)
{
    using Pool = chardsp::VoicePool<TestVoice, 3>;
    Pool pool;
    pool.prepare (1000.0f);

    pool.next();
    CHECK (pool.getVoice (0).renderCount == 1);
    CHECK (pool.getVoice (1).renderCount == 1);
    CHECK (pool.getVoice (2).renderCount == 1);

    pool.start (60, 0.8f);
    CHECK (pool.findVoice (60) == 0);
    CHECK (pool.getVoice (0).startCount == 1);
    CHECK (near (pool.getVoice (0).velocity, 0.8f));
    CHECK (pool.getActiveVoiceCount() == 1);

    pool.start (60, 0.4f);
    CHECK (pool.getVoice (0).startCount == 1);
    pool.start (62, 0.7f);
    pool.start (64, 0.6f);
    CHECK (pool.getActiveVoiceCount() == 3);
    CHECK (pool.findVoice (64) == 2);
    CHECK (pool.isKeyHeld (60));

    pool.stop (62);
    CHECK (pool.getVoice (1).releaseCount == 1);
    pool.start (62, 0.9f);
    CHECK (pool.getVoice (1).forcedReleaseCount == 1);
    CHECK (near (pool.getVoice (1).lastForcedReleaseSeconds, 0.002f));
    CHECK (pool.getVoice (1).startCount == 1);
    pool.next();
    CHECK (pool.getVoice (1).startCount == 1);
    pool.next();
    CHECK (pool.getVoice (1).startCount == 2);
    CHECK (pool.getVoice (1).lastRenderedStartCount == 1);
    pool.next();
    CHECK (pool.getVoice (1).lastRenderedStartCount == 2);
    CHECK (pool.findVoice (62) == 1);

    // A sounding release tail stays reserved, so another key uses a free slot.
    pool.prepare (1000.0f);
    pool.start (60, 1.0f);
    pool.stop (60);

    for (int i = 0; i < 3; ++i)
        pool.next();

    CHECK (pool.getActiveVoiceCount() == 1);
    CHECK (pool.findVoice (60) == 0);
    pool.start (62, 1.0f);
    CHECK (pool.findVoice (60) == 0);
    CHECK (pool.findVoice (62) == 1);

    // Unlike Nomad's event-only allocator, the C++ pool can ask the voice when
    // its tail is actually available and reclaim it immediately.
    pool.prepare (1000.0f);
    pool.start (60, 1.0f);
    pool.stop (60);

    for (int i = 0; i < 3; ++i)
        pool.next();

    CHECK (pool.findVoice (60) == 0);
    pool.next();
    CHECK (pool.findVoice (60) == -1);

    // An active same-key release tail force-releases and restarts its existing
    // slot after exactly 2 ms.
    pool.prepare (1000.0f);
    pool.start (60, 1.0f);
    pool.stop (60);
    pool.start (60, 0.25f);
    CHECK (pool.getVoice (0).forcedReleaseCount == 1);
    pool.next();
    CHECK (pool.getVoice (0).startCount == 1);
    pool.next();
    CHECK (pool.getVoice (0).startCount == 2);
    CHECK (pool.getVoice (0).lastRenderedStartCount == 1);
    pool.next();
    CHECK (pool.getVoice (0).lastRenderedStartCount == 2);
    CHECK (near (pool.getVoice (0).velocity, 0.25f));

    // Releasing a pending key before its delayed restart cancels it.
    pool.stop (60);
    pool.start (60, 1.0f);
    pool.stop (60);
    pool.next();
    pool.next();
    CHECK (pool.getVoice (0).startCount == 2);

    // Any stolen voice uses the same delayed fade, not only same-key
    // retriggers. The new voice starts after the old voice reports availability.
    using SingleVoicePool = chardsp::VoicePool<TestVoice, 1>;
    SingleVoicePool single;
    single.prepare (1000.0f);
    single.start (60, 1.0f);
    single.start (67, 0.5f);
    CHECK (single.isPending (67));
    CHECK (single.findVoice (60) == -1);
    CHECK (single.findVoice (67) == 0);
    CHECK (single.getVoice (0).forcedReleaseCount == 1);
    CHECK (near (single.getVoice (0).lastForcedReleaseSeconds, 0.002f));
    single.next();
    CHECK (single.getVoice (0).startCount == 1);
    single.next();
    CHECK (single.getVoice (0).startCount == 2);
    CHECK (! single.isPending (67));
    CHECK (single.getVoice (0).lastRenderedStartCount == 1);
    single.next();
    CHECK (single.getVoice (0).lastRenderedStartCount == 2);

    // The steal fade is configurable, and zero selects immediate replacement.
    single.prepare (1000.0f);
    single.setStealFadeTime (0.004f);
    CHECK (near (single.getStealFadeTime(), 0.004f));
    single.start (60, 1.0f);
    single.start (62, 1.0f);

    for (int i = 0; i < 3; ++i)
    {
        single.next();
        CHECK (single.getVoice (0).startCount == 1);
    }

    single.next();
    CHECK (single.getVoice (0).startCount == 2);
    single.setStealFadeTime (0.0f);
    single.start (64, 1.0f);
    CHECK (single.getVoice (0).startCount == 3);
    CHECK (near (single.getVoice (0).lastForcedReleaseSeconds, 0.0f));
    single.setStealFadeTime (std::numeric_limits<float>::quiet_NaN());
    CHECK (near (single.getStealFadeTime(), 0.0f));

    // Each slot owns its pending start. Fast notes can therefore fade multiple
    // stolen voices concurrently without a shared timer or heap allocation.
    using PairPool = chardsp::VoicePool<TestVoice, 2>;
    PairPool pair;
    pair.prepare (1000.0f);
    pair.start (60, 1.0f);
    pair.start (62, 1.0f);
    pair.start (64, 1.0f);
    pair.start (65, 1.0f);
    CHECK (pair.findVoice (64) == 0);
    CHECK (pair.findVoice (65) == 1);
    CHECK (pair.getVoice (0).startCount == 1);
    CHECK (pair.getVoice (1).startCount == 1);
    pair.next();
    pair.next();
    CHECK (pair.getVoice (0).startCount == 2);
    CHECK (pair.getVoice (1).startCount == 2);

    // When every slot is already fading, the latest note replaces the oldest
    // pending start without restarting the old voice's fade.
    pair.prepare (1000.0f);
    pair.start (60, 1.0f);
    pair.start (62, 1.0f);
    pair.start (64, 1.0f);
    pair.start (65, 1.0f);
    pair.start (67, 1.0f);
    CHECK (pair.findVoice (64) == -1);
    CHECK (pair.findVoice (67) == 0);
    CHECK (pair.getVoice (0).forcedReleaseCount == 1);

    // A burst much larger than the voice count stays bounded. With two slots,
    // the two newest pending keys survive until the original voices become available.
    pair.prepare (1000.0f);
    pair.start (60, 1.0f);
    pair.start (62, 1.0f);

    for (int note = 64; note < 84; ++note)
        pair.start (note, 1.0f);

    CHECK (pair.getActiveVoiceCount() == 2);
    CHECK (pair.getAllocatedVoiceCount() == 2);
    CHECK (pair.findVoice (82) >= 0);
    CHECK (pair.findVoice (83) >= 0);

    for (int note = 64; note < 82; ++note)
        CHECK (pair.findVoice (note) == -1);

    pair.next();
    pair.next();
    CHECK ((pair.getVoice (0).note == 82
            && pair.getVoice (1).note == 83)
           || (pair.getVoice (0).note == 83
               && pair.getVoice (1).note == 82));
    CHECK (pair.getVoice (0).startCount == 2);
    CHECK (pair.getVoice (1).startCount == 2);

    // A note-off received while a note is pending cancels its future start.
    single.prepare (1000.0f);
    single.setStealFadeTime (0.002f);
    single.start (60, 1.0f);
    single.start (62, 1.0f);
    single.stop (62);
    single.next();
    single.next();
    CHECK (single.getVoice (0).startCount == 1);
    CHECK (single.findVoice (62) == -1);

    // The priority order remains oldest releasing, then oldest held.
    pool.prepare (1000.0f);
    pool.start (60, 1.0f);
    pool.start (62, 1.0f);
    pool.start (64, 1.0f);
    pool.stop (60);
    pool.stop (62);
    pool.start (65, 1.0f);
    CHECK (pool.findVoice (65) == 0);
    CHECK (pool.findVoice (60) == -1);
    CHECK (pool.findVoice (62) == 1);

    // The oldest held voice is the final fallback. Additional fast notes can
    // reserve other slots while earlier stolen voices are still fading.
    pool.prepare (1000.0f);
    pool.start (60, 1.0f);
    pool.start (62, 1.0f);
    pool.start (64, 1.0f);
    pool.start (67, 1.0f);
    CHECK (pool.findVoice (67) == 0);
    CHECK (pool.findVoice (60) == -1);

    pool.stop (62);
    pool.start (62, 1.0f);
    pool.start (69, 1.0f);
    CHECK (pool.findVoice (69) >= 0);

    pool.allKeysOff();

    for (int i = 0; i < 4; ++i)
        pool.next();

    CHECK (pool.getActiveVoiceCount() == 0);

    pool.prepare (1000.0f);
    pool.start (69, 1.0f);
    pool.start (71, 1.0f);
    pool.forceReleaseAll();
    CHECK (pool.getVoice (0).forcedReleaseCount
           + pool.getVoice (1).forcedReleaseCount
           + pool.getVoice (2).forcedReleaseCount == 2);

    // Sustain defers voice release without introducing MIDI into the pool.
    pool.prepare (1000.0f);
    pool.start (72, 1.0f);
    pool.setSustain (true);
    pool.stop (72);
    CHECK (! pool.isKeyHeld (72));
    CHECK (pool.getVoice (0).releaseCount == 0);
    pool.setSustain (false);
    CHECK (pool.getVoice (0).releaseCount == 1);

    // Release duration comes from the Voice, not a guessed allocator timeout.
    // This tail deliberately lasts longer than Nomad's old ten-second memory.
    pool.prepare (10.0f);
    pool.start (74, 1.0f);
    pool.stop (74);
    pool.getVoice (0).releaseSamplesRemaining = 102;

    for (int i = 0; i < 101; ++i)
        pool.next();

    CHECK (pool.findVoice (74) == 0);
    pool.next();
    CHECK (pool.findVoice (74) == -1);

    // The key domain is caller-defined and has no MIDI or 32-key ceiling.
    // Only the four actual voices are stored, no matter how many source IDs
    // address the pool over time.
    using SourcePool = chardsp::VoicePool<SourceVoice, 4, SourceKey>;
    SourcePool sources;
    sources.prepare (1000.0f);

    for (uint64_t source = 0; source < 10000; ++source)
        sources.start ({ source, 100.0f + static_cast<float> (source % 900) },
                       0.5f);

    CHECK (sources.getAllocatedVoiceCount() == 4);
    CHECK (sources.getActiveVoiceCount() == 4);
    CHECK (sources.findVoice ({ 9999, 0.0f }) >= 0);
    CHECK (sources.findVoice ({ 0, 0.0f }) == -1);

    const auto latest = static_cast<size_t> (
        sources.findVoice ({ 9999, 0.0f }));
    const auto startsBeforeDuplicate = sources.getVoice (latest).startCount;
    sources.start ({ 9999, 777.0f }, 1.0f);
    CHECK (sources.getVoice (latest).startCount == startsBeforeDuplicate);
    sources.stop ({ 12, 0.0f });
    CHECK (sources.getActiveVoiceCount() == 4);

    const auto allocationsBeforeExercise = allocationCount.load();

    for (int event = 0; event < 100000; ++event)
    {
        pool.start (event, 0.75f);

        if (event % 3 == 0)
            pool.stop (event);

        pool.next();
    }

    CHECK (allocationCount.load() == allocationsBeforeExercise);
}

void testPortamento (choc::test::TestProgress& progress)
{
    chardsp::Portamento<float> glide;
    glide.prepare (100.0f);
    glide.reset (60.0f);
    glide.setTime (1.0f);
    glide.setShape (0.0f);
    glide.setTimeRateMorph (0.0f);
    glide.setTarget (72.0f, true);

    CHECK (near (glide.next(), 60.0f));

    for (int i = 1; i < 50; ++i)
        glide.next();

    CHECK (near (glide.next(), 66.0f));

    for (int i = 51; i <= 100; ++i)
        glide.next();

    CHECK (near (glide.next(), 72.0f));
    CHECK (! glide.isGliding());

    // At full rate scaling, a half-octave takes half the configured time.
    glide.reset (60.0f);
    glide.setTimeRateMorph (1.0f);
    glide.setTarget (66.0f, true);

    for (int i = 0; i < 50; ++i)
        glide.next();

    CHECK (! glide.isGliding());
    CHECK (near (glide.next(), 66.0f));

    // The curved endpoint is the active Nomad analog-shape equation.
    glide.reset (60.0f);
    glide.setTimeRateMorph (0.0f);
    glide.setShape (1.0f);
    glide.setTarget (72.0f, true);

    for (int i = 0; i < 50; ++i)
        glide.next();

    const auto strength = 6.0f;
    const auto analog = (1.0f - std::exp (-strength * 0.5f))
                      / (1.0f - std::exp (-strength));
    CHECK (near (glide.next(), 60.0f + 12.0f * analog, 0.00001f));

    glide.setTarget (48.0f, false);
    CHECK (near (glide.next(), 48.0f));
}

void testMonoVoiceController (choc::test::TestProgress& progress)
{
    using Controller = chardsp::MonoVoiceController<float, int, 8>;
    Controller mono;
    mono.prepare (1000.0f);
    mono.setLegato (true);
    mono.setPortamentoMode (chardsp::PortamentoMode::fingered);

    auto action = mono.start (1, 60.0f, 0.8f);
    CHECK (action.hasPitch() && action.trigger && ! action.glide);
    action = mono.start (2, 64.0f, 0.7f);
    CHECK (action.hasPitch() && ! action.trigger && action.glide);

    // Last-note priority falls back without retriggering the envelope.
    action = mono.stop (2);
    CHECK (action.hasPitch() && near (action.pitch, 60.0f));
    CHECK (! action.trigger && action.glide);

    // Legato delays the final gate-off by Nomad Cmajor's 24 ms grace window.
    CHECK (! mono.stop (1).shouldRelease());
    CHECK (mono.hasPendingGateOff());

    for (int i = 0; i < 23; ++i)
        CHECK (! mono.next().shouldRelease());

    CHECK (mono.next().shouldRelease());
    CHECK (! mono.isGateActive());

    // A new note in that grace window cancels the release and stays legato.
    mono.reset();
    mono.setLegato (true);
    mono.start (1, 60.0f, 1.0f);
    mono.stop (1);

    for (int i = 0; i < 10; ++i)
        mono.next();

    action = mono.start (2, 67.0f, 1.0f);
    CHECK (action.hasPitch() && ! action.trigger);
    CHECK (! mono.hasPendingGateOff());

    // Always glides across a gap; fingered glides only while a key is held.
    mono.reset();
    mono.setLegato (false);
    mono.setPortamentoMode (chardsp::PortamentoMode::always);
    mono.start (1, 60.0f, 1.0f);
    CHECK (mono.stop (1).shouldRelease());
    CHECK (mono.start (2, 72.0f, 1.0f).glide);
    mono.stop (2);
    mono.setPortamentoMode (chardsp::PortamentoMode::fingered);
    CHECK (! mono.start (3, 67.0f, 1.0f).glide);

    // Sustain owns only the gate; releasing it ends a voice with no held key.
    mono.reset();
    mono.setSustain (true);
    mono.start (1, 60.0f, 1.0f);
    CHECK (! mono.stop (1).shouldRelease());
    CHECK (mono.isGateActive());
    CHECK (mono.setSustain (false).shouldRelease());

    // Capacity is explicit and fixed rather than a MIDI-sized hidden limit.
    chardsp::MonoVoiceController<double, uint64_t, 2> small;
    small.prepare (48000.0);
    small.start (1000, 60.0, 1.0);
    small.start (2000, 62.0, 1.0);
    small.start (3000, 64.0, 1.0);
    CHECK (small.getHeldCount() == 2);
}

static_assert (std::is_nothrow_default_constructible_v<chardsp::ADSREnvelope<float>>);
static_assert (std::is_nothrow_default_constructible_v<chardsp::StateVariableFilter<float>>);
static_assert (std::is_nothrow_default_constructible_v<chardsp::ColoredNoise<float>>);
static_assert (std::is_nothrow_default_constructible_v<chardsp::Downsampler4x<float>>);
static_assert (std::is_nothrow_default_constructible_v<chardsp::Oscillator<float>>);
static_assert (std::is_nothrow_default_constructible_v<
               chardsp::VoicePool<TestVoice, 4>>);
static_assert (std::is_nothrow_default_constructible_v<
               chardsp::Portamento<double>>);
static_assert (std::is_nothrow_default_constructible_v<
               chardsp::MonoVoiceController<double, uint64_t, 64>>);
static_assert (std::is_nothrow_default_constructible_v<
               chardsp::BasicSynthVoice<double>>);

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
    choc::test::TestProgress progress;
    CHOC_CATEGORY (chardsp);

    { CHOC_TEST (smoothed_value); testSmoothedValue (progress); }
    { CHOC_TEST (elliptic_saw); testEllipticSaw (progress); }
    { CHOC_TEST (oscillator); testOscillator (progress); }
    { CHOC_TEST (choc_triangle_modulation); testChocTriangleModulation (progress); }
    { CHOC_TEST (adsr_nomad_parity); testADSREnvelopeParity (progress); }
    { CHOC_TEST (adsr_behavior); testADSREnvelopeBehavior (progress); }
    { CHOC_TEST (adsr_live_updates); testADSREnvelopeLiveUpdates (progress); }
    { CHOC_TEST (svf_nomad_parity); testStateVariableFilterParity (progress); }
    { CHOC_TEST (svf_behavior); testStateVariableFilterBehavior (progress); }
    { CHOC_TEST (colored_noise_nomad_parity); testColoredNoiseParity (progress); }
    { CHOC_TEST (colored_noise_behavior); testColoredNoiseBehavior (progress); }
    { CHOC_TEST (downsampler_nomad_parity); testDownsamplerParity (progress); }
    { CHOC_TEST (downsampler_behavior); testDownsamplerBehavior (progress); }
    { CHOC_TEST (double_precision_types); testDoublePrecisionTypes (progress); }
    { CHOC_TEST (voice_pool); testVoicePool (progress); }
    { CHOC_TEST (portamento); testPortamento (progress); }
    { CHOC_TEST (mono_voice_controller); testMonoVoiceController (progress); }

    progress.printReport();
    return progress.numFails == 0 ? 0 : 1;
}
