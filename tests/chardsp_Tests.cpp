#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <type_traits>

#include "chardsp/chardsp_ADSREnvelope.h"
#include "chardsp/chardsp_AllpassDiffuser.h"
#include "chardsp/chardsp_AnalysisBuffer.h"
#include "chardsp/chardsp_BasicSynthVoice.h"
#include "chardsp/chardsp_Biquad.h"
#include "chardsp/chardsp_ColoredNoise.h"
#include "chardsp/chardsp_ConstantQ.h"
#include "chardsp/chardsp_ConstantQStream.h"
#include "chardsp/chardsp_DCBlocker.h"
#include "chardsp/chardsp_DelayLine.h"
#include "chardsp/chardsp_Detectors.h"
#include "chardsp/chardsp_EllipticBLEP.h"
#include "chardsp/chardsp_FractionalDelayLine.h"
#include "chardsp/chardsp_Gain.h"
#include "chardsp/chardsp_MonoVoiceController.h"
#include "chardsp/chardsp_Oscillator.h"
#include "chardsp/chardsp_OscillatorEvents.h"
#include "chardsp/chardsp_OnePole.h"
#include "chardsp/chardsp_OscillatorPhase.h"
#include "chardsp/chardsp_Oversampler.h"
#include "chardsp/chardsp_Portamento.h"
#include "chardsp/chardsp_SamplePlayer.h"
#include "chardsp/chardsp_SimpleDownsampler.h"
#include "chardsp/chardsp_SmoothedValue.h"
#include "chardsp/chardsp_Spectrum.h"
#include "chardsp/chardsp_StateVariableFilter.h"
#include "chardsp/chardsp_VoicePool.h"
#include "choc/platform/choc_UnitTest.h"

namespace
{

std::atomic<size_t> allocationCount { 0 };

void testSmoothedValue (choc::test::TestProgress& progress)
{
    chardsp::SmoothedValue<float> value;
    CHOC_EXPECT_NEAR (value.getCurrent(), 0.0f, 0.000001f);
    CHOC_EXPECT_NEAR (value.next(), 0.0f, 0.000001f);

    value.reset (-2.0f);
    CHOC_EXPECT_NEAR (value.getCurrent(), -2.0f, 0.000001f);
    CHOC_EXPECT_NEAR (value.next(), -2.0f, 0.000001f);

    value.setTarget (3.0f, 0);
    CHOC_EXPECT_NEAR (value.getCurrent(), 3.0f, 0.000001f);
    CHOC_EXPECT_NEAR (value.next(), 3.0f, 0.000001f);

    value.setTarget (-1.0f, 1);
    CHOC_EXPECT_NEAR (value.next(), -1.0f, 0.000001f);
    CHOC_EXPECT_NEAR (value.next(), -1.0f, 0.000001f);

    value.reset (-1.0f);
    value.setTarget (1.0f, 4);
    CHOC_EXPECT_NEAR (value.next(), -0.5f, 0.000001f);
    CHOC_EXPECT_NEAR (value.next(), 0.0f, 0.000001f);
    CHOC_EXPECT_NEAR (value.next(), 0.5f, 0.000001f);
    CHOC_EXPECT_NEAR (value.next(), 1.0f, 0.000001f);
    CHOC_EXPECT_NEAR (value.getCurrent(), 1.0f, 0.000001f);
    CHOC_EXPECT_NEAR (value.next(), 1.0f, 0.000001f);

    value.reset (0.0f);
    value.setTarget (1.0f, 4);
    CHOC_EXPECT_NEAR (value.next(), 0.25f, 0.000001f);
    CHOC_EXPECT_NEAR (value.next(), 0.5f, 0.000001f);
    value.setTarget (-0.5f, 2);
    CHOC_EXPECT_NEAR (value.next(), 0.0f, 0.000001f);
    CHOC_EXPECT_NEAR (value.next(), -0.5f, 0.000001f);
    CHOC_EXPECT_NEAR (value.next(), -0.5f, 0.000001f);

    value.reset (1.0f);
    value.setTarget (16.0f, 4, chardsp::SmoothingMode::multiplicative);
    CHOC_EXPECT_NEAR (value.getTarget(), 16.0f, 0.000001f);
    CHOC_EXPECT_TRUE (value.isSmoothing());
    CHOC_EXPECT_NEAR (value.next(), 2.0f, 0.000001f);
    CHOC_EXPECT_NEAR (value.skip (2), 8.0f, 0.000001f);
    CHOC_EXPECT_NEAR (value.next(), 16.0f, 0.000001f);
    CHOC_EXPECT_FALSE (value.isSmoothing());

    value.reset (-1.0f);
    value.setTarget (-16.0f, 4,
                     chardsp::SmoothingMode::multiplicative);
    CHOC_EXPECT_NEAR (value.skip (4), -16.0f, 0.000001f);

    value.reset (-1.0f);
    value.setTarget (1.0f, 2,
                     chardsp::SmoothingMode::multiplicative);
    CHOC_EXPECT_NEAR (value.next(), 0.0f, 0.000001f);
    CHOC_EXPECT_NEAR (value.next(), 1.0f, 0.000001f);

    value.reset (0.0f);
    value.setTarget (4.0f, 4,
                     chardsp::SmoothingMode::multiplicative);
    CHOC_EXPECT_NEAR (value.next(), 1.0f, 0.000001f);
    CHOC_EXPECT_NEAR (value.skip (100), 4.0f, 0.000001f);
    CHOC_EXPECT_FALSE (value.isSmoothing());

    value.reset (std::numeric_limits<float>::infinity());
    CHOC_EXPECT_NEAR (value.getCurrent(), 0.0f, 0.000001f);
}

void testDelayLineAndCrossfade (choc::test::TestProgress& progress)
{
    chardsp::DelayLine<float, 4> delay;
    static_assert (decltype (delay)::maximumDelay == 3);
    delay.reset();
    delay.setDelay (0.0f);
    CHOC_EXPECT_NEAR (delay.process (1.0f), 1.0f, 0.000001f);
    delay.setDelay (1.0f);
    CHOC_EXPECT_NEAR (delay.process (2.0f), 1.0f, 0.000001f);
    CHOC_EXPECT_NEAR (delay.read (0), 2.0f, 0.000001f);
    CHOC_EXPECT_NEAR (delay.read (1), 1.0f, 0.000001f);

    delay.write (3.0f);
    delay.write (4.0f);
    delay.write (5.0f);
    CHOC_EXPECT_NEAR (delay.read (0), 5.0f, 0.000001f);
    CHOC_EXPECT_NEAR (delay.read (3), 2.0f, 0.000001f);
    CHOC_EXPECT_NEAR (delay.read (100), 2.0f, 0.000001f);

    delay.reset();
    delay.write (0.0f);
    delay.write (10.0f);
    CHOC_EXPECT_NEAR (delay.readLinear (0.5f), 5.0f, 0.000001f);
    CHOC_EXPECT_NEAR (delay.readLinear (
        std::numeric_limits<float>::infinity()), 10.0f, 0.000001f);
    delay.setDelay (100.0f);
    CHOC_EXPECT_NEAR (delay.getDelay(), 3.0f, 0.000001f);

    chardsp::DelayLine<double, 8> doubleDelay;
    doubleDelay.setDelay (1.5);
    doubleDelay.write (1.0);
    doubleDelay.write (2.0);
    CHOC_EXPECT_TRUE (std::isfinite (doubleDelay.read()));

    CHOC_EXPECT_NEAR (chardsp::linearCrossfade (-1.0f, 1.0f, 0.25f), -0.5f, 0.000001f);
    CHOC_EXPECT_NEAR (chardsp::linearCrossfade (-1.0f, 1.0f, -2.0f), -1.0f, 0.000001f);
    CHOC_EXPECT_NEAR (chardsp::equalPowerCrossfade (1.0f, 0.0f, 0.0f), 1.0f, 0.000001f);
    CHOC_EXPECT_NEAR (chardsp::equalPowerCrossfade (0.0f, 1.0f, 1.0f), 1.0f, 0.000001f);
    CHOC_EXPECT_NEAR (chardsp::equalPowerCrossfade (1.0f, 1.0f, 0.5f), std::sqrt (2.0f), 0.000002f);

    const auto allocationsBefore = allocationCount.load();

    for (int i = 0; i < 100000; ++i)
    {
        delay.write (static_cast<float> (i));
        (void) delay.readLinear (1.25f);
    }

    CHOC_EXPECT_EQ (allocationCount.load(), allocationsBefore);
}

void testFractionalDelayLine (choc::test::TestProgress& progress)
{
    chardsp::FractionalDelayLine<float> unprepared;
    CHOC_EXPECT_EQ (unprepared.getMaximumDelay(), 0);
    unprepared.write (1.0f);
    CHOC_EXPECT_NEAR (unprepared.read(), 0.0f, 0.000001f);
    CHOC_EXPECT_NEAR (unprepared.read (10), 0.0f, 0.000001f);
    CHOC_EXPECT_NEAR (unprepared.readCubic (2.5f), 0.0f, 0.000001f);

    chardsp::FractionalDelayLine<float> line;
    line.prepare (64);
    CHOC_EXPECT_EQ (line.getMaximumDelay(), 64);
    line.setDelay (10.0f);

    for (int i = 0; i < 32; ++i)
        CHOC_EXPECT_NEAR (line.process (i == 0 ? 1.0f : 0.0f),
                          i == 10 ? 1.0f : 0.0f, 0.000001f);

    // Catmull-Rom reproduces a ramp exactly; the newest sample has delay zero.
    line.reset();

    for (int i = 0; i < 200; ++i)
        line.write (static_cast<float> (i));

    CHOC_EXPECT_NEAR (line.readCubic (10.25f), 188.75f, 0.001f);
    CHOC_EXPECT_NEAR (line.readCubic (0.0f), 199.0f, 0.000001f);
    CHOC_EXPECT_NEAR (line.read (1), 198.0f, 0.000001f);

    constexpr double sampleRate = 48000.0;
    constexpr double frequency = 1000.0;
    constexpr double omega = 6.28318530717958647692 * frequency / sampleRate;
    chardsp::FractionalDelayLine<float> floatSine;
    chardsp::FractionalDelayLine<double> doubleSine;
    floatSine.prepare (256);
    doubleSine.prepare (256);

    for (int i = 0; i < 4096; ++i)
    {
        const auto sample = std::sin (omega * static_cast<double> (i));
        floatSine.write (static_cast<float> (sample));
        doubleSine.write (sample);
    }

    const auto expected = std::sin (omega * (4095.0 - 100.5));
    const auto floatCubic = static_cast<double> (floatSine.readCubic (100.5f));
    const auto floatLinear = 0.5 * (static_cast<double> (floatSine.read (100))
                                  + static_cast<double> (floatSine.read (101)));
    CHOC_EXPECT_TRUE (std::abs (floatCubic - expected) < 0.001);
    CHOC_EXPECT_TRUE (std::abs (floatCubic - expected)
                      < std::abs (floatLinear - expected));
    // Kernel truncation rather than precision sets the floor at this frequency
    // ratio, so double matches the float result instead of beating it.
    CHOC_EXPECT_TRUE (std::abs (doubleSine.readCubic (100.5) - expected)
                      < 0.00001);
    CHOC_EXPECT_TRUE (std::abs (doubleSine.readCubic (100.5) - floatCubic)
                      < 0.000001);

    chardsp::FractionalDelayLine<float> wrap;
    wrap.prepare (8);

    for (int i = 0; i < 100; ++i)
        wrap.write (static_cast<float> (i));

    CHOC_EXPECT_NEAR (wrap.read (8), 91.0f, 0.000001f);
    wrap.setDelay (8.0f);
    CHOC_EXPECT_NEAR (wrap.read(), 91.0f, 0.000001f);
    CHOC_EXPECT_NEAR (wrap.readCubic (7.5f), 91.5f, 0.001f);

    wrap.reset();
    wrap.write (std::numeric_limits<float>::quiet_NaN());
    CHOC_EXPECT_NEAR (wrap.read (0), 0.0f, 0.000001f);
    wrap.write (std::numeric_limits<float>::infinity());
    CHOC_EXPECT_NEAR (wrap.read (0), 0.0f, 0.000001f);
    wrap.write (5.0f);
    wrap.setDelay (std::numeric_limits<float>::quiet_NaN());
    CHOC_EXPECT_NEAR (wrap.getDelay(), 0.0f, 0.000001f);
    CHOC_EXPECT_NEAR (wrap.read(), 5.0f, 0.000001f);
    wrap.setDelay (100.0f);
    CHOC_EXPECT_NEAR (wrap.getDelay(), 8.0f, 0.000001f);
    CHOC_EXPECT_NEAR (wrap.read (100), wrap.read (8), 0.000001f);

    const auto allocationsBefore = allocationCount.load();
    line.setDelay (10.5f);

    for (int i = 0; i < 100000; ++i)
        (void) line.process (static_cast<float> (i % 16));

    CHOC_EXPECT_EQ (allocationCount.load(), allocationsBefore);
}

void testAllpassDiffuser (choc::test::TestProgress& progress)
{
    chardsp::AllpassDiffuser<float> unprepared;
    CHOC_EXPECT_NEAR (unprepared.process (0.25f), 0.25f, 0.000001f);

    chardsp::AllpassDiffuser<float> pureDelay;
    pureDelay.prepare (48000.0f, 20.0f);
    pureDelay.setGain (0.0f);
    CHOC_EXPECT_NEAR (pureDelay.getGain(), 0.0f, 0.000001f);
    pureDelay.setStageDelay (0, 1.0f);
    pureDelay.setStageDelay (1, 2.0f);
    pureDelay.setStageDelay (2, 3.0f);
    pureDelay.setStageDelay (3, 4.0f);
    pureDelay.setStageDelay (4, 5.0f);
    float peak = 0.0f;
    int peakIndex = 0;
    double absoluteSum = 0.0;

    for (int i = 0; i < 1024; ++i)
    {
        const auto output = pureDelay.process (i == 0 ? 1.0f : 0.0f);
        absoluteSum += std::abs (static_cast<double> (output));

        if (std::abs (output) > peak)
        {
            peak = std::abs (output);
            peakIndex = i;
        }
    }

    CHOC_EXPECT_EQ (peakIndex, 480);
    CHOC_EXPECT_NEAR (peak, 1.0f, 0.000001f);
    CHOC_EXPECT_NEAR (absoluteSum, 1.0, 0.000001);

    // A Schroeder stage is allpass, so its impulse response carries unit energy.
    chardsp::AllpassDiffuser<float, 1> single;
    single.prepare (48000.0f, 10.0f);
    single.setStageDelay (0, 1.0f);
    single.setGain (0.5f);
    double singleEnergy = 0.0;

    for (int i = 0; i < 960; ++i)
    {
        const auto output = single.process (i == 0 ? 1.0f : 0.0f);
        singleEnergy += static_cast<double> (output) * output;
    }

    CHOC_EXPECT_NEAR (singleEnergy, 1.0, 0.001);

    // The prototype's stage times at full diffusion: still allpass, and smeared.
    chardsp::AllpassDiffuser<float> diffuser;
    diffuser.prepare (48000.0f, 20.0f);
    diffuser.setStageDelay (0, 2.3f);
    diffuser.setStageDelay (1, 5.1f);
    diffuser.setStageDelay (2, 9.7f);
    diffuser.setStageDelay (3, 15.3f);
    diffuser.setGain (0.62f);
    CHOC_EXPECT_NEAR (diffuser.getGain(), 0.62f, 0.000001f);
    double energy = 0.0;
    float diffuserPeak = 0.0f;

    for (int i = 0; i < 96000; ++i)
    {
        const auto output = diffuser.process (i == 0 ? 1.0f : 0.0f);
        energy += static_cast<double> (output) * output;
        diffuserPeak = std::max (diffuserPeak, std::abs (output));
    }

    CHOC_EXPECT_NEAR (energy, 1.0, 0.01);
    CHOC_EXPECT_TRUE (diffuserPeak < 0.5f);
    CHOC_EXPECT_TRUE (std::isfinite (diffuser.process (
        std::numeric_limits<float>::quiet_NaN())));
    diffuser.setGain (std::numeric_limits<float>::infinity());
    CHOC_EXPECT_NEAR (diffuser.getGain(), 0.0f, 0.000001f);
    diffuser.setGain (2.0f);
    CHOC_EXPECT_NEAR (diffuser.getGain(), 0.98f, 0.000001f);
    diffuser.setGain (-2.0f);
    CHOC_EXPECT_NEAR (diffuser.getGain(), -0.98f, 0.000001f);
    diffuser.setGain (0.62f);
    diffuser.reset();

    const auto allocationsBefore = allocationCount.load();

    for (int i = 0; i < 100000; ++i)
        (void) diffuser.process (std::sin (0.013f * static_cast<float> (i)));

    CHOC_EXPECT_EQ (allocationCount.load(), allocationsBefore);

    chardsp::AllpassDiffuser<double, 2> doubleDiffuser;
    doubleDiffuser.prepare (96000.0, 5.0);
    doubleDiffuser.setStageDelay (0, 1.0);
    doubleDiffuser.setStageDelay (1, 2.0);
    doubleDiffuser.setGain (0.7);
    CHOC_EXPECT_TRUE (std::isfinite (doubleDiffuser.process (1.0)));
}

void testOnePole (choc::test::TestProgress& progress)
{
    chardsp::OnePole<float> unprepared;
    CHOC_EXPECT_NEAR (unprepared.getCutoff(), 1000.0f, 0.000001f);
    CHOC_EXPECT_TRUE (unprepared.getMode() == chardsp::OnePoleMode::lowpass);
    CHOC_EXPECT_TRUE (unprepared.process (1.0f) > 0.0f);

    chardsp::OnePole<float> lowpass;
    lowpass.prepare (48000.0f);
    lowpass.setCutoff (1000.0f);
    float output = 0.0f;

    for (int i = 0; i < 2000; ++i)
        output = lowpass.process (1.0f);

    CHOC_EXPECT_NEAR (output, 1.0f, 0.0001f);

    chardsp::OnePole<float> highpass;
    highpass.prepare (48000.0f);
    highpass.setMode (chardsp::OnePoleMode::highpass);
    highpass.setCutoff (1000.0f);
    CHOC_EXPECT_TRUE (highpass.getMode() == chardsp::OnePoleMode::highpass);

    for (int i = 0; i < 2000; ++i)
        output = highpass.process (1.0f);

    CHOC_EXPECT_TRUE (std::abs (output) < 0.0001f);

    const auto measurePeak = [] (chardsp::OnePole<float>& filter,
                                 float frequency)
    {
        float result = 0.0f;

        for (int i = 0; i < 8192; ++i)
        {
            const auto sample = filter.process (std::sin (
                6.283185307179586f * frequency
                * static_cast<float> (i) / 48000.0f));

            if (i >= 4096)
                result = std::max (result, std::abs (sample));
        }

        return result;
    };

    lowpass.reset();
    CHOC_EXPECT_NEAR (measurePeak (lowpass, 1000.0f), 0.70710678f, 0.1f);
    lowpass.setCutoff (20000.0f);
    lowpass.reset();
    CHOC_EXPECT_NEAR (measurePeak (lowpass, 1000.0f), 1.0f, 0.005f);

    lowpass.setCutoff (-5.0f);
    CHOC_EXPECT_NEAR (lowpass.getCutoff(), 1.0f, 0.000001f);
    lowpass.setCutoff (1000000.0f);
    CHOC_EXPECT_NEAR (lowpass.getCutoff(), 23520.0f, 0.01f);
    lowpass.setCutoff (std::numeric_limits<float>::quiet_NaN());
    CHOC_EXPECT_NEAR (lowpass.getCutoff(), 1000.0f, 0.000001f);
    CHOC_EXPECT_TRUE (std::isfinite (lowpass.process (
        std::numeric_limits<float>::infinity())));

    lowpass.reset();
    (void) lowpass.process (1.0f);

    for (int i = 0; i < 2000; ++i)
        output = lowpass.process (0.0f);

    CHOC_EXPECT_TRUE (output == 0.0f);

    const auto allocationsBefore = allocationCount.load();

    for (int i = 0; i < 100000; ++i)
        (void) lowpass.process (std::sin (0.013f * static_cast<float> (i)));

    CHOC_EXPECT_EQ (allocationCount.load(), allocationsBefore);

    chardsp::OnePole<double> doublePole;
    doublePole.prepare (96000.0);
    doublePole.setMode (chardsp::OnePoleMode::highpass);
    doublePole.setCutoff (50.0);
    CHOC_EXPECT_TRUE (std::isfinite (doublePole.process (1.0)));
    doublePole.prepare (std::numeric_limits<double>::quiet_NaN());
    CHOC_EXPECT_NEAR (doublePole.getCutoff(), 50.0, 0.000001);
}

void testDCBlockerAndDetectors (choc::test::TestProgress& progress)
{
    chardsp::DCBlocker<float> blocker;
    blocker.prepare (1000.0f);
    blocker.setCutoff (10.0f);
    float output = 0.0f;

    for (int i = 0; i < 2000; ++i)
        output = blocker.process (1.0f);

    CHOC_EXPECT_TRUE (std::abs (output) < 0.000001f);

    blocker.reset();
    double inputEnergy = 0.0;
    double outputEnergy = 0.0;

    for (int i = 0; i < 4000; ++i)
    {
        const auto input = std::sin (
            6.283185307179586f * 100.0f * static_cast<float> (i)
            / 1000.0f);
        const auto filtered = blocker.process (input);

        if (i >= 1000)
        {
            inputEnergy += static_cast<double> (input) * input;
            outputEnergy += static_cast<double> (filtered) * filtered;
        }
    }

    CHOC_EXPECT_TRUE (std::sqrt (outputEnergy / inputEnergy) > 0.98);
    blocker.setCutoff (0.0f);
    CHOC_EXPECT_NEAR (blocker.process (0.25f), 0.25f, 0.000001f);
    blocker.setCutoff (std::numeric_limits<float>::quiet_NaN());
    CHOC_EXPECT_NEAR (blocker.getCutoff(), 20.0f, 0.000001f);
    CHOC_EXPECT_TRUE (std::isfinite (blocker.process (
        std::numeric_limits<float>::infinity())));

    chardsp::PeakFollower<float> peak;
    peak.prepare (10.0f, 1.0f);
    CHOC_EXPECT_NEAR (peak.process (-1.0f), 1.0f, 0.000001f);

    for (int i = 0; i < 10; ++i)
        output = peak.process (0.0f);

    CHOC_EXPECT_NEAR (output, std::exp (-1.0f), 0.000002f);

    chardsp::RMSFollower<float> rms;
    rms.prepare (100.0f, 0.1f);

    for (int i = 0; i < 100; ++i)
        output = rms.process (-1.0f);

    CHOC_EXPECT_NEAR (output, 1.0f, 0.0001f);
    rms.reset();
    rms.prepare (48000.0f, 1.0f);

    for (int i = 0; i < 480000; ++i)
        output = rms.process (std::sin (
            6.283185307179586f * 997.0f * static_cast<float> (i)
            / 48000.0f));

    CHOC_EXPECT_NEAR (output, 0.70710678f, 0.001f);

    chardsp::EnvelopeFollower<float> envelope;
    envelope.prepare (10.0f, 1.0f, 1.0f);

    for (int i = 0; i < 10; ++i)
        output = envelope.process (1.0f);

    CHOC_EXPECT_NEAR (output, 1.0f - std::exp (-1.0f), 0.000002f);

    for (int i = 0; i < 10; ++i)
        output = envelope.process (0.0f);

    CHOC_EXPECT_NEAR (output, (1.0f - std::exp (-1.0f)) * std::exp (-1.0f), 0.000002f);
    CHOC_EXPECT_NEAR (envelope.process (
        std::numeric_limits<float>::quiet_NaN()), output * std::exp (-0.1f), 0.000002f);

    chardsp::DCBlocker<double> doubleBlocker;
    chardsp::PeakFollower<double> doublePeak;
    chardsp::RMSFollower<double> doubleRMS;
    chardsp::EnvelopeFollower<double> doubleEnvelope;
    doubleBlocker.prepare (48000.0);
    doublePeak.prepare (48000.0);
    doubleRMS.prepare (48000.0);
    doubleEnvelope.prepare (48000.0);
    CHOC_EXPECT_TRUE (std::isfinite (doubleBlocker.process (1.0)));
    CHOC_EXPECT_TRUE (std::isfinite (doublePeak.process (1.0)));
    CHOC_EXPECT_TRUE (std::isfinite (doubleRMS.process (1.0)));
    CHOC_EXPECT_TRUE (std::isfinite (doubleEnvelope.process (1.0)));
}

template <typename SampleType>
SampleType measureBiquadRMS (
    chardsp::BiquadCoefficients<SampleType> coefficients,
    SampleType sampleRate, SampleType frequency)
{
    chardsp::Biquad<SampleType> filter;
    filter.setCoefficients (coefficients);
    constexpr int discarded = 8192;
    constexpr int measured = 16384;
    double sumSquares = 0.0;

    for (int i = 0; i < discarded + measured; ++i)
    {
        const auto input = std::sin (
            static_cast<SampleType> (6.28318530717958647692)
            * frequency * static_cast<SampleType> (i) / sampleRate);
        const auto output = filter.process (input);

        if (i >= discarded)
            sumSquares += static_cast<double> (output) * output;
    }

    return static_cast<SampleType> (
        std::sqrt (sumSquares / measured));
}

void testBiquad (choc::test::TestProgress& progress)
{
    using Coefficients = chardsp::BiquadCoefficients<float>;
    constexpr float sampleRate = 48000.0f;
    constexpr float cutoff = 2000.0f;
    constexpr float q = 0.70710678f;

    const auto lowpass = Coefficients::lowpass (sampleRate, cutoff, q);
    const auto highpass = Coefficients::highpass (sampleRate, cutoff, q);
    const auto bandpass = Coefficients::bandpass (sampleRate, cutoff, q);
    const auto notch = Coefficients::notch (sampleRate, cutoff, q);
    const auto peak = Coefficients::peak (
        sampleRate, cutoff, 1.0f, 6.0f);
    const auto lowShelf = Coefficients::lowShelf (
        sampleRate, cutoff, 6.0f);
    const auto highShelf = Coefficients::highShelf (
        sampleRate, cutoff, 6.0f);

    for (const auto& coefficients : {
             lowpass, highpass, bandpass, notch,
             peak, lowShelf, highShelf })
        CHOC_EXPECT_TRUE (coefficients.isStable());

    CHOC_EXPECT_TRUE (measureBiquadRMS (lowpass, sampleRate, 200.0f) > 0.69f);
    CHOC_EXPECT_TRUE (measureBiquadRMS (lowpass, sampleRate, 12000.0f) < 0.02f);
    CHOC_EXPECT_TRUE (measureBiquadRMS (highpass, sampleRate, 200.0f) < 0.02f);
    CHOC_EXPECT_TRUE (measureBiquadRMS (highpass, sampleRate, 12000.0f) > 0.69f);
    CHOC_EXPECT_TRUE (measureBiquadRMS (bandpass, sampleRate, cutoff) > 0.68f);
    CHOC_EXPECT_TRUE (measureBiquadRMS (bandpass, sampleRate, 12000.0f) < 0.2f);
    CHOC_EXPECT_TRUE (measureBiquadRMS (notch, sampleRate, cutoff) < 0.02f);
    CHOC_EXPECT_TRUE (measureBiquadRMS (notch, sampleRate, 12000.0f) > 0.65f);
    CHOC_EXPECT_TRUE (measureBiquadRMS (peak, sampleRate, cutoff) > 1.35f);
    CHOC_EXPECT_TRUE (measureBiquadRMS (lowShelf, sampleRate, 200.0f)
           > measureBiquadRMS (lowShelf, sampleRate, 12000.0f) * 1.8f);
    CHOC_EXPECT_TRUE (measureBiquadRMS (highShelf, sampleRate, 12000.0f)
           > measureBiquadRMS (highShelf, sampleRate, 200.0f) * 1.8f);

    const auto invalid = Coefficients::lowpass (
        0.0f, cutoff, q);
    CHOC_EXPECT_TRUE (invalid.isStable());
    CHOC_EXPECT_NEAR (invalid.b0, 1.0f, 0.000001f);

    chardsp::Biquad<float> filter;
    Coefficients unstable;
    unstable.a2 = 2.0f;
    CHOC_EXPECT_FALSE (filter.setCoefficients (unstable));
    CHOC_EXPECT_NEAR (filter.process (0.25f), 0.25f, 0.000001f);
    CHOC_EXPECT_TRUE (filter.setCoefficients (lowpass));
    const auto first = filter.process (1.0f);
    (void) filter.process (0.0f);
    CHOC_EXPECT_FALSE (filter.setCoefficients (unstable));
    CHOC_EXPECT_NEAR (filter.process (0.0f), 0.0f, 0.000001f);
    CHOC_EXPECT_NEAR (filter.process (0.0f), 0.0f, 0.000001f);
    CHOC_EXPECT_TRUE (filter.setCoefficients (lowpass));
    filter.reset();
    CHOC_EXPECT_NEAR (filter.process (1.0f), first, 0.000001f);

    const auto allocationsBefore = allocationCount.load();
    bool finite = true;

    for (int i = 0; i < 200000; ++i)
        finite = finite && std::isfinite (filter.process (
            std::sin (0.013f * static_cast<float> (i))));

    CHOC_EXPECT_TRUE (finite);
    CHOC_EXPECT_EQ (allocationCount.load(), allocationsBefore);

    using DoubleCoefficients = chardsp::BiquadCoefficients<double>;
    chardsp::Biquad<double> doubleFilter;
    CHOC_EXPECT_TRUE (doubleFilter.setCoefficients (
        DoubleCoefficients::highShelf (96000.0, 10000.0, -3.0)));
    CHOC_EXPECT_TRUE (std::isfinite (doubleFilter.process (1.0)));
}

void testSamplePlayer (choc::test::TestProgress& progress)
{
    std::array<float, 3> samples { 0.0f, 1.0f, 0.0f };
    chardsp::SamplePlayer<float> player;
    player.setSampleData (samples);
    player.setRate (0.5f);
    player.start();
    CHOC_EXPECT_TRUE (player.isPlaying());
    CHOC_EXPECT_NEAR (player.process(), 0.0f, 0.000001f);
    CHOC_EXPECT_NEAR (player.process(), 0.5f, 0.000001f);
    CHOC_EXPECT_NEAR (player.process(), 1.0f, 0.000001f);
    CHOC_EXPECT_NEAR (player.process(), 0.5f, 0.000001f);
    samples[2] = 0.25f;
    CHOC_EXPECT_NEAR (player.process(), 0.25f, 0.000001f);
    CHOC_EXPECT_NEAR (player.process(), 0.25f, 0.000001f);
    CHOC_EXPECT_FALSE (player.isPlaying());
    CHOC_EXPECT_NEAR (player.process(), 0.0f, 0.000001f);

    player.start (1.0f);
    player.stop();
    CHOC_EXPECT_NEAR (player.process(), 0.0f, 0.000001f);
    player.setRate (-1.0f);
    CHOC_EXPECT_NEAR (player.getRate(), 0.0f, 0.000001f);
    player.setPosition (100.0f);
    CHOC_EXPECT_FALSE (player.isPlaying());
    player.reset();
    CHOC_EXPECT_NEAR (player.getPosition(), 0.0f, 0.000001f);

    std::array<double, 2> doubleSamples { -1.0, 1.0 };
    chardsp::SamplePlayer<double> doublePlayer;
    doublePlayer.setSampleData (doubleSamples);
    doublePlayer.setRate (0.5);
    doublePlayer.start (0.5);
    CHOC_EXPECT_TRUE (std::abs (doublePlayer.process()) < 0.000001);

    const auto allocationsBefore = allocationCount.load();

    for (int i = 0; i < 10000; ++i)
    {
        player.start();
        (void) player.process();
    }

    CHOC_EXPECT_EQ (allocationCount.load(), allocationsBefore);
}

float measureOversamplingRoundTrip (
    chardsp::OversamplingFactor factor, float frequency)
{
    constexpr uint32_t blockSize = 64;
    constexpr uint32_t blockCount = 256;
    constexpr uint32_t discardedBlocks = 64;
    constexpr float sampleRate = 48000.0f;
    chardsp::Oversampler<float> oversampler;
    oversampler.prepare (1, blockSize, factor);
    choc::buffer::ChannelArrayBuffer<float> input (1, blockSize);
    choc::buffer::ChannelArrayBuffer<float> output (1, blockSize);
    double inputSquares = 0.0;
    double outputSquares = 0.0;

    for (uint32_t block = 0; block < blockCount; ++block)
    {
        for (uint32_t frame = 0; frame < blockSize; ++frame)
        {
            const auto sampleIndex = block * blockSize + frame;
            input.getSample (0, frame) = std::sin (
                6.283185307179586f * frequency
                * static_cast<float> (sampleIndex) / sampleRate);
        }

        (void) oversampler.upsample (input);
        oversampler.downsample (output);

        if (block >= discardedBlocks)
            for (uint32_t frame = 0; frame < blockSize; ++frame)
            {
                const auto inputSample = input.getSample (0, frame);
                const auto outputSample = output.getSample (0, frame);
                inputSquares += static_cast<double> (inputSample)
                              * inputSample;
                outputSquares += static_cast<double> (outputSample)
                               * outputSample;
            }
    }

    return static_cast<float> (std::sqrt (outputSquares / inputSquares));
}

float measureOversamplingDownsampleGain (
    chardsp::OversamplingFactor factor, float frequency)
{
    constexpr uint32_t blockSize = 64;
    constexpr uint32_t blockCount = 256;
    constexpr uint32_t discardedBlocks = 64;
    constexpr float sampleRate = 48000.0f;
    chardsp::Oversampler<float> oversampler;
    oversampler.prepare (1, blockSize, factor);
    choc::buffer::ChannelArrayBuffer<float> input (1, blockSize);
    choc::buffer::ChannelArrayBuffer<float> output (1, blockSize);
    input.clear();
    double inputSquares = 0.0;
    double outputSquares = 0.0;
    const auto highRate = sampleRate * static_cast<uint32_t> (factor);

    for (uint32_t block = 0; block < blockCount; ++block)
    {
        auto highRateBlock = oversampler.upsample (input);

        for (uint32_t frame = 0;
             frame < highRateBlock.getNumFrames(); ++frame)
        {
            const auto sampleIndex = block
                    * highRateBlock.getNumFrames() + frame;
            const auto sample = std::sin (
                6.283185307179586f * frequency
                * static_cast<float> (sampleIndex) / highRate);
            highRateBlock.getSample (0, frame) = sample;

            if (block >= discardedBlocks)
                inputSquares += static_cast<double> (sample) * sample;
        }

        oversampler.downsample (output);

        if (block >= discardedBlocks)
            for (uint32_t frame = 0; frame < blockSize; ++frame)
            {
                const auto sample = output.getSample (0, frame);
                outputSquares += static_cast<double> (sample) * sample;
            }
    }

    const auto decimation = static_cast<double> (
        static_cast<uint32_t> (factor));
    return static_cast<float> (
        std::sqrt (outputSquares / (inputSquares / decimation)));
}

float measureDFTMagnitude (const std::array<float, 4096>& samples,
                           float frequency,
                           float sampleRate)
{
    std::complex<double> sum;

    for (size_t i = 0; i < samples.size(); ++i)
    {
        const auto phase = -6.28318530717958647692
                         * static_cast<double> (frequency)
                         * static_cast<double> (i)
                         / static_cast<double> (sampleRate);
        sum += static_cast<double> (samples[i])
             * std::complex<double> (std::cos (phase), std::sin (phase));
    }

    return static_cast<float> (
        std::abs (sum) * 2.0 / static_cast<double> (samples.size()));
}

std::array<float, 4096> renderCubic (
    bool oversampled, size_t& allocationsDuringProcessing)
{
    constexpr uint32_t blockSize = 64;
    constexpr uint32_t warmupSamples = 4096;
    constexpr uint32_t measuredSamples = 4096;
    constexpr uint32_t totalSamples = warmupSamples + measuredSamples;
    constexpr float frequency = 9000.0f;
    constexpr float sampleRate = 48000.0f;
    std::array<float, measuredSamples> result {};
    choc::buffer::ChannelArrayBuffer<float> input (1, blockSize);
    choc::buffer::ChannelArrayBuffer<float> output (1, blockSize);
    chardsp::Oversampler<float> oversampler;
    oversampler.prepare (1, blockSize, chardsp::OversamplingFactor::x4);
    const auto allocationsBefore = allocationCount.load();

    for (uint32_t blockStart = 0; blockStart < totalSamples;
         blockStart += blockSize)
    {
        for (uint32_t frame = 0; frame < blockSize; ++frame)
        {
            const auto sampleIndex = blockStart + frame;
            input.getSample (0, frame) = std::sin (
                6.283185307179586f * frequency
                * static_cast<float> (sampleIndex) / sampleRate);
        }

        if (oversampled)
        {
            auto highRate = oversampler.upsample (input);

            for (uint32_t frame = 0; frame < highRate.getNumFrames(); ++frame)
            {
                const auto sample = highRate.getSample (0, frame);
                highRate.getSample (0, frame) = sample * sample * sample;
            }

            oversampler.downsample (output);
        }
        else
        {
            for (uint32_t frame = 0; frame < blockSize; ++frame)
            {
                const auto sample = input.getSample (0, frame);
                output.getSample (0, frame) = sample * sample * sample;
            }
        }

        if (blockStart >= warmupSamples)
            for (uint32_t frame = 0; frame < blockSize; ++frame)
                result[blockStart - warmupSamples + frame]
                    = output.getSample (0, frame);
    }

    allocationsDuringProcessing = allocationCount.load() - allocationsBefore;
    return result;
}

void testOversampler (choc::test::TestProgress& progress)
{
    for (const auto factor : {
             chardsp::OversamplingFactor::x2,
             chardsp::OversamplingFactor::x4 })
    {
        chardsp::Oversampler<float> oversampler;
        CHOC_EXPECT_FALSE (oversampler.prepare (0, 64, factor));
        CHOC_EXPECT_FALSE (oversampler.prepare (1, 0, factor));
        CHOC_EXPECT_FALSE (oversampler.prepare (1, 64,
            static_cast<chardsp::OversamplingFactor> (3)));
        CHOC_EXPECT_TRUE (oversampler.prepare (2, 128, factor));
        CHOC_EXPECT_TRUE (oversampler.getFactor() == factor);
        CHOC_EXPECT_EQ (oversampler.getNumChannels(), 2);
        CHOC_EXPECT_EQ (oversampler.getMaximumBlockSize(), 128);
        CHOC_EXPECT_TRUE (oversampler.canProcess (2, 128));
        CHOC_EXPECT_FALSE (oversampler.canProcess (1, 128));
        CHOC_EXPECT_FALSE (oversampler.canProcess (2, 129));

        choc::buffer::ChannelArrayBuffer<float> input (2, 128);
        choc::buffer::ChannelArrayBuffer<float> output (2, 128);
        input.clear();
        input.getSample (0, 0) = 1.0f;
        const auto allocationsBefore = allocationCount.load();
        auto highRate = oversampler.upsample (input);
        const auto didDownsample = oversampler.downsample (output);
        const auto allocationsAfterProcess = allocationCount.load();
        CHOC_EXPECT_EQ (highRate.getNumChannels(), 2);
        CHOC_EXPECT_EQ (highRate.getNumFrames(), 128u * static_cast<uint32_t> (factor));
        CHOC_EXPECT_TRUE (didDownsample);
        CHOC_EXPECT_TRUE (allocationsAfterProcess == allocationsBefore);

        uint32_t peakFrame = 0;
        float peak = 0.0f;

        for (uint32_t frame = 0; frame < output.getNumFrames(); ++frame)
        {
            if (std::abs (output.getSample (0, frame)) > peak)
            {
                peak = std::abs (output.getSample (0, frame));
                peakFrame = frame;
            }

            CHOC_EXPECT_NEAR (output.getSample (1, frame), 0.0f, 0.000001f);
        }

        CHOC_EXPECT_EQ (peakFrame, oversampler.getLatency());
        CHOC_EXPECT_TRUE (peak > 0.5f);

        const auto allocationsBeforeReset = allocationCount.load();
        oversampler.reset();
        CHOC_EXPECT_TRUE (allocationCount.load() == allocationsBeforeReset);
        input.clear();
        highRate = oversampler.upsample (input);
        CHOC_EXPECT_TRUE (oversampler.downsample (output));

        for (uint32_t channel = 0; channel < 2; ++channel)
            for (uint32_t frame = 0; frame < 128; ++frame)
                CHOC_EXPECT_NEAR (output.getSample (channel, frame), 0.0f, 0.000001f);
    }

    CHOC_EXPECT_TRUE (measureOversamplingRoundTrip (
        chardsp::OversamplingFactor::x2, 1000.0f) > 0.99f);
    CHOC_EXPECT_TRUE (measureOversamplingRoundTrip (
        chardsp::OversamplingFactor::x4, 1000.0f) > 0.99f);
    CHOC_EXPECT_TRUE (measureOversamplingDownsampleGain (
        chardsp::OversamplingFactor::x2, 1000.0f) > 0.99f);
    CHOC_EXPECT_TRUE (measureOversamplingDownsampleGain (
        chardsp::OversamplingFactor::x4, 1000.0f) > 0.99f);
    CHOC_EXPECT_TRUE (measureOversamplingDownsampleGain (
        chardsp::OversamplingFactor::x2, 30000.0f) < 0.05f);
    CHOC_EXPECT_TRUE (measureOversamplingDownsampleGain (
        chardsp::OversamplingFactor::x4, 30000.0f) < 0.05f);

    size_t directAllocations = 0;
    size_t oversampledAllocations = 0;
    const auto direct = renderCubic (false, directAllocations);
    const auto oversampled = renderCubic (true, oversampledAllocations);
    const auto directAlias = measureDFTMagnitude (
        direct, 21000.0f, 48000.0f);
    const auto oversampledAlias = measureDFTMagnitude (
        oversampled, 21000.0f, 48000.0f);
    CHOC_EXPECT_TRUE (directAlias > 0.2f);
    CHOC_EXPECT_TRUE (oversampledAlias < directAlias * 0.25f);
    CHOC_EXPECT_EQ (directAllocations, 0);
    CHOC_EXPECT_EQ (oversampledAllocations, 0);

    chardsp::Oversampler<double> doubleOversampler;
    CHOC_EXPECT_TRUE (doubleOversampler.prepare (
        1, 16, chardsp::OversamplingFactor::x2));
    choc::buffer::ChannelArrayBuffer<double> doubleInput (1, 16);
    choc::buffer::ChannelArrayBuffer<double> doubleOutput (1, 16);
    doubleInput.clear();
    doubleInput.getSample (0, 0) = 1.0;
    auto doubleHighRate = doubleOversampler.upsample (doubleInput);
    CHOC_EXPECT_EQ (doubleHighRate.getNumFrames(), 32);
    CHOC_EXPECT_TRUE (doubleOversampler.downsample (doubleOutput));
    CHOC_EXPECT_TRUE (std::isfinite (doubleOutput.getSample (0, 0)));
}

void testEllipticSaw (choc::test::TestProgress& progress)
{
    chardsp::EllipticSawOscillator<float> unprepared;
    unprepared.setFrequency (440.0f);
    CHOC_EXPECT_NEAR (unprepared.next(), 0.0f, 0.000001f);

    chardsp::EllipticSawOscillator<float> oscillator;
    oscillator.prepare (48000.0f);
    oscillator.setFrequency (0.0f);

    for (int i = 0; i < 128; ++i)
        CHOC_EXPECT_NEAR (oscillator.next(), 0.0f, 0.000001f);

    oscillator.setFrequency (440.0f);
    float minimum = std::numeric_limits<float>::infinity();
    float maximum = -std::numeric_limits<float>::infinity();
    float previous = oscillator.next();
    float maximumDelta = 0.0f;
    CHOC_EXPECT_TRUE (std::isfinite (previous));

    const auto allocationsBeforeRender = allocationCount.load();

    for (int i = 1; i < 200000; ++i)
    {
        const auto sample = oscillator.next();
        CHOC_EXPECT_TRUE (std::isfinite (sample));
        minimum = std::min (minimum, sample);
        maximum = std::max (maximum, sample);
        maximumDelta = std::max (maximumDelta, std::abs (sample - previous));
        previous = sample;
    }

    CHOC_EXPECT_TRUE (allocationCount.load() == allocationsBeforeRender);
    CHOC_EXPECT_TRUE (maximum - minimum > 1.0f);
    CHOC_EXPECT_TRUE (maximumDelta < 2.5f);

    oscillator.setFrequency (0.0f);
    CHOC_EXPECT_NEAR (oscillator.next(), 0.0f, 0.000001f);
    CHOC_EXPECT_NEAR (oscillator.next(), 0.0f, 0.000001f);

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
        CHOC_EXPECT_TRUE (std::isfinite (limitedSample));
        CHOC_EXPECT_NEAR (limitedSample, explicitSample, 0.000001f);
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
            CHOC_EXPECT_NEAR (splitRender.next(), expected[i], 0.000001f);
}

void testOscillatorPhaseAndEllipticCorrection (
    choc::test::TestProgress& progress)
{
    chardsp::OscillatorPhase<float> phase;
    phase.prepare (8.0f);
    phase.setFrequency (2.0f);
    phase.reset (0.9f);

    const auto advance = phase.advanceWithStatus();
    CHOC_EXPECT_TRUE (advance.wrapped);
    CHOC_EXPECT_NEAR (advance.startPhase, 0.9f, 0.000001f);
    CHOC_EXPECT_NEAR (advance.phase, 0.15f, 0.000001f);
    CHOC_EXPECT_NEAR (advance.wrapOffset, 0.4f, 0.000001f);

    chardsp::EllipticBLEPCorrection<float> unprepared;
    CHOC_EXPECT_NEAR (unprepared.process (0.25f), 0.25f, 0.000001f);

    chardsp::EllipticBLEPCorrection<float> correction;
    correction.prepare (48000.0f);
    correction.beginSample();
    correction.addStep (0.25f, 2.0f);
    correction.addSlopeJump (0.75f, -0.1f);

    const auto allocationsBeforeRender = allocationCount.load();
    float peak = 0.0f;

    for (int i = 0; i < 512; ++i)
    {
        const auto sample = correction.process (0.0f);
        CHOC_EXPECT_TRUE (std::isfinite (sample));
        peak = std::max (peak, std::abs (sample));
        correction.beginSample();
    }

    CHOC_EXPECT_TRUE (peak > 0.01f);
    CHOC_EXPECT_TRUE (allocationCount.load() == allocationsBeforeRender);

    correction.reset();
    correction.beginSample();
    CHOC_EXPECT_TRUE (std::isfinite (correction.process (0.0f)));

    std::array<chardsp::PulseEdgeEvent<float>, 3> events {};
    size_t eventCount = 0;
    const auto capture = [&] (chardsp::PulseEdgeEvent<float> event)
    {
        events[eventCount++] = event;
    };

    chardsp::forEachMovingPulseEdge (
        0.4f, 0.6f, 0.5f, 0.5f, 0.0f, 1.0f, capture);
    CHOC_EXPECT_TRUE (eventCount == 1);
    CHOC_EXPECT_TRUE (std::abs (events[0].offset - 0.5f) <= 0.000001f);
    CHOC_EXPECT_TRUE (std::abs (events[0].step + 2.0f) <= 0.000001f);

    eventCount = 0;
    chardsp::forEachMovingPulseEdge (
        0.5f, 0.51f, 0.4f, 0.6f, 0.0f, 1.0f, capture);
    CHOC_EXPECT_TRUE (eventCount == 1);
    CHOC_EXPECT_TRUE (std::abs (events[0].offset - 0.5263158f)
                      <= 0.000001f);
    CHOC_EXPECT_TRUE (std::abs (events[0].step - 2.0f) <= 0.000001f);

    eventCount = 0;
    chardsp::forEachMovingPulseEdge (
        0.9f, 1.1f, 0.5f, 0.5f, 0.0f, 1.0f, capture);
    CHOC_EXPECT_TRUE (eventCount == 1);
    CHOC_EXPECT_TRUE (std::abs (events[0].offset - 0.5f) <= 0.000001f);
    CHOC_EXPECT_TRUE (std::abs (events[0].step - 2.0f) <= 0.000001f);

    eventCount = 0;
    chardsp::forEachMovingPulseEdge (
        0.4f, 1.1f, 0.5f, 0.5f, 0.0f, 1.0f, capture);
    CHOC_EXPECT_TRUE (eventCount == 2);
    CHOC_EXPECT_TRUE (std::abs (events[0].offset - 1.0f / 7.0f)
                      <= 0.000001f);
    CHOC_EXPECT_TRUE (std::abs (events[0].step + 2.0f) <= 0.000001f);
    CHOC_EXPECT_TRUE (std::abs (events[1].offset - 6.0f / 7.0f)
                      <= 0.000001f);
    CHOC_EXPECT_TRUE (std::abs (events[1].step - 2.0f) <= 0.000001f);

    CHOC_EXPECT_TRUE (allocationCount.load() == allocationsBeforeRender);

    chardsp::EllipticPulseOscillator<float> movingPulse;
    movingPulse.setPulseWidth (0.4f);
    movingPulse.prepare (48000.0f);
    movingPulse.setFrequency (480.0f);
    movingPulse.reset (0.5f);
    movingPulse.setPulseWidth (0.6f);

    chardsp::EllipticBLEPCorrection<float> movingReference;
    movingReference.prepare (48000.0f);
    movingReference.beginSample();
    movingReference.addStep (0.5263158f, 2.0f);
    const auto allocationsBeforeMovingRender = allocationCount.load();

    const auto movingSample = movingPulse.next();
    const auto referenceSample = movingReference.process (1.0f);
    const auto allocationsAfterMovingRender = allocationCount.load();
    CHOC_EXPECT_NEAR (movingSample, referenceSample, 0.000002f);
    CHOC_EXPECT_EQ (allocationsAfterMovingRender,
                    allocationsBeforeMovingRender);
}

void testGenericOscillators (choc::test::TestProgress& progress)
{
    chardsp::NaiveSawOscillator<float> saw;
    saw.prepare (8.0f);
    saw.setFrequency (1.0f);
    CHOC_EXPECT_NEAR (saw.next(), 0.75f, 0.000001f);
    saw.reset();
    saw.setDirection (chardsp::SawDirection::ascending);
    CHOC_EXPECT_NEAR (saw.next(), -0.75f, 0.000001f);

    chardsp::NaivePulseOscillator<float> pulse;
    pulse.prepare (8.0f);
    pulse.setFrequency (1.0f);
    pulse.setPulseWidth (0.5f);
    CHOC_EXPECT_NEAR (pulse.next(), 1.0f, 0.000001f);
    pulse.setPulseWidth (-1.0f);
    CHOC_EXPECT_NEAR (pulse.getPulseWidth(), 0.001f, 0.000001f);
    pulse.setPulseWidth (2.0f);
    CHOC_EXPECT_NEAR (pulse.getPulseWidth(), 0.999f, 0.000001f);

    chardsp::NaiveTriangleOscillator<float> triangle;
    triangle.prepare (8.0f);
    triangle.setFrequency (1.0f);
    CHOC_EXPECT_NEAR (triangle.next(), -0.5f, 0.000001f);

    chardsp::SineOscillator<float> sine;
    sine.prepare (8.0f);
    sine.setFrequency (1.0f);
    CHOC_EXPECT_NEAR (sine.next(), std::sqrt (0.5f), 0.000002f);

    chardsp::PolyBLEPSawOscillator<float> polySaw;
    chardsp::PolyBLEPPulseOscillator<float> polyPulse;
    chardsp::PolyBLEPTriangleOscillator<float> polyTriangle;
    chardsp::EllipticSawOscillator<float> ellipticSaw;
    chardsp::EllipticPulseOscillator<float> ellipticPulse;
    chardsp::EllipticTriangleOscillator<float> ellipticTriangle;

    polySaw.setDirection (chardsp::SawDirection::ascending);
    polyPulse.setPulseWidth (0.37f);
    ellipticSaw.setDirection (chardsp::SawDirection::ascending);
    ellipticPulse.setPulseWidth (0.37f);

    const auto exercise = [&] (auto& oscillator)
    {
        oscillator.prepare (48000.0f);
        oscillator.setFrequency (997.0f);
        oscillator.reset (0.37f);
        const auto allocationsBeforeRender = allocationCount.load();
        bool finite = true;
        float peak = 0.0f;

        for (int i = 0; i < 100000; ++i)
        {
            const auto sample = oscillator.next();
            finite = finite && std::isfinite (sample);
            peak = std::max (peak, std::abs (sample));
        }

        CHOC_EXPECT_TRUE (finite);
        CHOC_EXPECT_TRUE (peak > 0.25f);
        CHOC_EXPECT_TRUE (peak < 2.5f);
        CHOC_EXPECT_EQ (allocationCount.load(), allocationsBeforeRender);
    };

    exercise (polySaw);
    exercise (polyPulse);
    exercise (polyTriangle);
    exercise (ellipticSaw);
    exercise (ellipticPulse);
    exercise (ellipticTriangle);

    chardsp::EllipticTriangleOscillator<float> advanced;
    chardsp::EllipticTriangleOscillator<float> rendered;
    advanced.prepare (48000.0f);
    rendered.prepare (48000.0f);
    advanced.setFrequency (997.0f);
    rendered.setFrequency (997.0f);
    advanced.reset (0.37f);
    rendered.reset (0.37f);

    for (int i = 0; i < 137; ++i)
    {
        advanced.advance();
        (void) rendered.next();
    }

    for (int i = 0; i < 64; ++i)
        CHOC_EXPECT_NEAR (advanced.next(), rendered.next(), 0.000002f);
}

void testBasicSynthVoice (choc::test::TestProgress& progress)
{
    chardsp::BasicSynthVoice<float> fullVelocity;
    chardsp::BasicSynthVoice<float> halfVelocity;
    chardsp::BasicSynthVoice<float> ignoredVelocity;
    chardsp::BasicSynthVoice<float> squaredVelocity;
    fullVelocity.prepare (48000.0f, 3);
    halfVelocity.prepare (48000.0f, 3);
    ignoredVelocity.prepare (48000.0f, 3);
    squaredVelocity.prepare (48000.0f, 3);

    for (auto* voice : { &fullVelocity, &halfVelocity, &ignoredVelocity,
                         &squaredVelocity })
    {
        voice->setOscillator1Level (0.0f);
        voice->setOscillator2Waveform (chardsp::BasicSynthWaveform::sine);
        voice->setOscillator2Level (1.0f);
        voice->setNoiseLevel (0.0f);
        voice->setFilter (chardsp::FilterMode::lowpass, 12000.0f, 0.8f);
        voice->setAttack (0.0f);
        voice->setDecay (0.0f);
        voice->setSustain (1.0f);
    }

    fullVelocity.start (69.0f, 1.0f);
    halfVelocity.start (69.0f, 0.5f);
    ignoredVelocity.setVelocitySensitivity (0.0f);
    ignoredVelocity.start (69.0f, 0.1f);
    squaredVelocity.setVelocityCurve (2.0f);
    squaredVelocity.start (69.0f, 0.5f);

    for (int i = 0; i < 128; ++i)
    {
        const auto full = fullVelocity.next();
        CHOC_EXPECT_NEAR (halfVelocity.next(), full * 0.5f, 0.000002f);
        CHOC_EXPECT_NEAR (ignoredVelocity.next(), full, 0.000002f);
        CHOC_EXPECT_NEAR (squaredVelocity.next(), full * 0.25f, 0.000002f);
    }

    fullVelocity.forceRelease (0.0f);
    CHOC_EXPECT_TRUE (fullVelocity.isAvailable());

    constexpr std::array waveforms {
        chardsp::BasicSynthWaveform::saw,
        chardsp::BasicSynthWaveform::pulse,
        chardsp::BasicSynthWaveform::triangle,
        chardsp::BasicSynthWaveform::sine
    };

    for (const auto waveform : waveforms)
    {
        chardsp::BasicSynthVoice<float> voice;
        voice.prepare (48000.0f, 0);
        voice.setOscillator1Waveform (waveform);
        voice.setOscillator1PulseWidth (0.35f);
        voice.setOscillator1Level (1.0f);
        voice.setOscillator2Level (0.0f);
        voice.setNoiseLevel (0.0f);
        voice.setAttack (0.0f);
        voice.setDecay (0.0f);
        voice.setSustain (1.0f);
        voice.start (69.0f, 1.0f);

        float peak = 0.0f;
        for (int sample = 0; sample < 256; ++sample)
        {
            const auto output = voice.next();
            CHOC_EXPECT_TRUE (std::isfinite (output));
            peak = std::max (peak, std::abs (output));
        }
        CHOC_EXPECT_TRUE (peak > 0.01f);
    }

    fullVelocity.setOscillator1PulseWidth (-1.0f);
    halfVelocity.setOscillator2PulseWidth (2.0f);
    CHOC_EXPECT_NEAR (fullVelocity.getOscillator1PulseWidth(), 0.001f, 0.000001f);
    CHOC_EXPECT_NEAR (halfVelocity.getOscillator2PulseWidth(), 0.999f, 0.000001f);
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

            CHOC_EXPECT_NEAR (envelope.next(), reference[i], 0.0000002f);
        }

        CHOC_EXPECT_NEAR (envelope.next(), 0.0f, 0.000001f);
        CHOC_EXPECT_FALSE (envelope.isActive());
        CHOC_EXPECT_NEAR (envelope.getCurrent(), 0.0f, 0.000001f);
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
    CHOC_EXPECT_TRUE (envelope.isActive());
    CHOC_EXPECT_NEAR (envelope.next(), 1.0f / 3.0f, 0.000001f);
    CHOC_EXPECT_NEAR (envelope.next(), 2.0f / 3.0f, 0.000001f);
    CHOC_EXPECT_NEAR (envelope.next(), 1.0f, 0.000001f);
    CHOC_EXPECT_NEAR (envelope.next(), 0.7f, 0.000001f);
    CHOC_EXPECT_NEAR (envelope.next(), 0.4f, 0.000001f);

    for (int i = 0; i < 8; ++i)
        CHOC_EXPECT_NEAR (envelope.next(), 0.4f, 0.000001f);

    envelope.release();
    CHOC_EXPECT_NEAR (envelope.next(), 0.2f, 0.000001f);
    CHOC_EXPECT_NEAR (envelope.next(), 0.0f, 0.000001f);
    CHOC_EXPECT_FALSE (envelope.isActive());

    envelope.setAttack (0.0f);
    envelope.setDecay (0.0f);
    envelope.setSustain (0.3f);
    envelope.setRelease (0.0f);
    envelope.trigger();
    CHOC_EXPECT_TRUE (envelope.isActive());
    CHOC_EXPECT_NEAR (envelope.getCurrent(), 0.3f, 0.000001f);
    CHOC_EXPECT_NEAR (envelope.next(), 0.3f, 0.000001f);
    envelope.release();
    CHOC_EXPECT_NEAR (envelope.getCurrent(), 0.0f, 0.000001f);
    CHOC_EXPECT_FALSE (envelope.isActive());

    envelope.reset();
    envelope.setAttack (0.4f);
    envelope.setDecay (0.0f);
    envelope.setSustain (0.25f);
    envelope.retrigger();
    CHOC_EXPECT_NEAR (envelope.next(), 0.25f, 0.000001f);

    envelope.reset();
    envelope.setAttack (0.4f);
    envelope.setDecay (0.0f);
    envelope.setSustain (0.4f);
    envelope.trigger();
    envelope.next();
    envelope.next();
    const auto beforeRetrigger = envelope.getCurrent();
    envelope.retrigger();
    CHOC_EXPECT_NEAR (envelope.getCurrent(), beforeRetrigger, 0.000001f);
    CHOC_EXPECT_TRUE (envelope.next() >= beforeRetrigger);

    envelope.setAttack (0.0f);
    envelope.setDecay (0.0f);
    envelope.setSustain (0.25f);
    envelope.trigger();
    envelope.forceRelease (0.4f);
    CHOC_EXPECT_NEAR (envelope.next(), 0.1875f, 0.000001f);
    CHOC_EXPECT_NEAR (envelope.next(), 0.125f, 0.000001f);
    CHOC_EXPECT_NEAR (envelope.next(), 0.0625f, 0.000001f);
    CHOC_EXPECT_NEAR (envelope.next(), 0.0f, 0.000001f);
    CHOC_EXPECT_FALSE (envelope.isActive());

    envelope.trigger();
    envelope.forceRelease (0.0f);
    CHOC_EXPECT_NEAR (envelope.getCurrent(), 0.0f, 0.000001f);
    CHOC_EXPECT_FALSE (envelope.isActive());
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
    CHOC_EXPECT_NEAR (envelope.next(), 0.1f, 0.000001f);
    CHOC_EXPECT_NEAR (envelope.next(), 0.2f, 0.000001f);

    envelope.setAttack (0.2f);
    CHOC_EXPECT_NEAR (envelope.getCurrent(), 0.2f, 0.000001f);
    CHOC_EXPECT_NEAR (envelope.next(), 0.6f, 0.000001f);
    CHOC_EXPECT_NEAR (envelope.next(), 1.0f, 0.000001f);
    CHOC_EXPECT_NEAR (envelope.next(), 0.92f, 0.000001f);

    envelope.setSustain (0.8f);
    envelope.setDecay (0.2f);
    CHOC_EXPECT_NEAR (envelope.getCurrent(), 0.92f, 0.000001f);
    CHOC_EXPECT_NEAR (envelope.next(), 0.86f, 0.000001f);
    CHOC_EXPECT_NEAR (envelope.next(), 0.8f, 0.000001f);

    envelope.release();
    CHOC_EXPECT_NEAR (envelope.next(), 0.72f, 0.000001f);
    envelope.setRelease (0.2f);
    CHOC_EXPECT_NEAR (envelope.getCurrent(), 0.72f, 0.000001f);
    CHOC_EXPECT_NEAR (envelope.next(), 0.36f, 0.000001f);
    CHOC_EXPECT_NEAR (envelope.next(), 0.0f, 0.000001f);

    envelope.setAttack (1.0f);
    envelope.setDecay (0.0f);
    envelope.setSustain (1.0f);
    envelope.trigger();
    envelope.next();
    const auto beforeShapeChange = envelope.getCurrent();
    envelope.setShape (chardsp::EnvelopeShape::exponential);
    CHOC_EXPECT_NEAR (envelope.getCurrent(), beforeShapeChange, 0.000001f);
    CHOC_EXPECT_TRUE (std::isfinite (envelope.next()));

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

    CHOC_EXPECT_TRUE (allFinite);
    CHOC_EXPECT_EQ (allocationCount.load(), allocationsBeforeRender);
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
            CHOC_EXPECT_NEAR (filter.process (i == 0 ? 1.0f : 0.0f), reference[i], 0.000001f);
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
        CHOC_EXPECT_TRUE (lowpassLow > 0.65f);
        CHOC_EXPECT_TRUE (lowpassHigh < 0.02f);
        CHOC_EXPECT_TRUE (highpassLow < 0.04f);
        CHOC_EXPECT_TRUE (highpassHigh > 0.65f);
        CHOC_EXPECT_TRUE (bandpassLow < 0.16f);
        CHOC_EXPECT_TRUE (bandpassCenter > 0.48f);
        CHOC_EXPECT_TRUE (bandpassHigh < 0.07f);
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
    CHOC_EXPECT_NEAR (first.process (0.25f), second.process (0.25f), 0.000001f);

    first.reset();
    second.reset();
    CHOC_EXPECT_NEAR (first.process (1.0f), second.process (1.0f), 0.000001f);

    first.reset();
    bandpass.reset();
    first.setMode (chardsp::FilterMode::bandpass);
    CHOC_EXPECT_NEAR (first.process (1.0f), bandpass.process (1.0f), 0.000001f);

    chardsp::StateVariableFilter<float> clamped;
    chardsp::StateVariableFilter<float> explicitLimits;
    clamped.prepare (48000.0f);
    explicitLimits.prepare (48000.0f);
    clamped.setCutoffAndQ (-100.0f, -5.0f);
    explicitLimits.setCutoffAndQ (0.0f, 0.000001f);
    CHOC_EXPECT_NEAR (clamped.process (1.0f), explicitLimits.process (1.0f), 0.000001f);

    clamped.reset();
    explicitLimits.reset();
    clamped.setCutoffAndQ (1000000.0f, 0.7f);
    explicitLimits.setCutoffAndQ (48000.0f * 0.48f, 0.7f);

    for (int i = 0; i < 128; ++i)
        CHOC_EXPECT_NEAR (clamped.process (i == 0 ? 1.0f : 0.0f), explicitLimits.process (i == 0 ? 1.0f : 0.0f), 0.000001f);

    chardsp::StateVariableFilter<float> silence;
    silence.prepare (96000.0f);
    silence.setCutoffAndQ (20000.0f, 0.5f);
    bool stableSilence = true;

    for (int i = 0; i < 100000; ++i)
        stableSilence = stableSilence && silence.process (0.0f) == 0.0f;

    CHOC_EXPECT_TRUE (stableSilence);

    chardsp::StateVariableFilter<float> decaying;
    decaying.prepare (48000.0f);
    decaying.setCutoffAndQ (1000.0f, 0.70710678f);
    CHOC_EXPECT_NE (decaying.process (1.0f), 0.0f);

    bool reachedExactZero = false;
    for (int i = 0; i < 100000; ++i)
    {
        if (decaying.process (0.0f) == 0.0f)
        {
            reachedExactZero = true;
            break;
        }
    }

    CHOC_EXPECT_TRUE (reachedExactZero);
    CHOC_EXPECT_EQ (decaying.process (0.0f), 0.0f);

    chardsp::StateVariableFilter<float> nonFiniteInput;
    nonFiniteInput.prepare (48000.0f);
    nonFiniteInput.setCutoffAndQ (1000.0f, 0.70710678f);
    nonFiniteInput.process (1.0f);
    const auto nanOutput = nonFiniteInput.process (
        std::numeric_limits<float>::quiet_NaN());
    const auto infinityOutput = nonFiniteInput.process (
        std::numeric_limits<float>::infinity());
    CHOC_EXPECT_TRUE (std::isfinite (nanOutput));
    CHOC_EXPECT_TRUE (std::isfinite (infinityOutput));

    for (int i = 0; i < 1024; ++i)
        CHOC_EXPECT_TRUE (std::isfinite (nonFiniteInput.process (0.0f)));

    const auto allocationsBeforeRender = allocationCount.load();
    bool allFinite = true;

    for (int i = 0; i < 200000; ++i)
    {
        const auto input = std::sin (0.013f * static_cast<float> (i));
        allFinite = allFinite && std::isfinite (silence.process (input));
    }

    CHOC_EXPECT_TRUE (allFinite);
    CHOC_EXPECT_EQ (allocationCount.load(), allocationsBeforeRender);

    chardsp::StateVariableFilter<float> allOutputs;
    chardsp::StateVariableFilter<float> selectedLow;
    chardsp::StateVariableFilter<float> selectedBand;
    chardsp::StateVariableFilter<float> selectedHigh;

    for (auto* filter : {
             &allOutputs, &selectedLow, &selectedBand, &selectedHigh })
    {
        filter->prepare (48000.0f);
        filter->setCutoffAndQ (3000.0f, 0.8f);
    }

    selectedLow.setMode (chardsp::FilterMode::lowpass);
    selectedBand.setMode (chardsp::FilterMode::bandpass);
    selectedHigh.setMode (chardsp::FilterMode::highpass);

    for (int i = 0; i < 1024; ++i)
    {
        const auto input = std::sin (0.027f * static_cast<float> (i));
        const auto outputs = allOutputs.processAll (input);
        CHOC_EXPECT_NEAR (outputs.lowpass, selectedLow.process (input), 0.000001f);
        CHOC_EXPECT_NEAR (outputs.bandpass, selectedBand.process (input), 0.000001f);
        CHOC_EXPECT_NEAR (outputs.highpass, selectedHigh.process (input), 0.000001f);
        CHOC_EXPECT_NEAR (outputs.notch, outputs.lowpass + outputs.highpass, 0.000001f);
    }
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
            CHOC_EXPECT_NEAR (noise.next(), reference, 0.000001f);
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
        CHOC_EXPECT_NEAR (noise.next(), expected, 0.000001f);

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
        CHOC_EXPECT_NEAR (switching.next(), endpoints[selected], 0.000001f);
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
        CHOC_EXPECT_TRUE (finiteAndBounded);
        CHOC_EXPECT_TRUE (std::abs (mean) < 0.15f);
        CHOC_EXPECT_TRUE (rms > 0.01f && rms < 0.9f);
    }

    CHOC_EXPECT_EQ (allocationCount.load(), allocationsBeforeRender);
}

void testSimpleDownsamplerParity (choc::test::TestProgress& progress)
{
    constexpr std::array<float, 12> reference {
        0.002049217699f, 0.04932555556f, 0.1835883409f, 0.07104226947f,
        -0.1017052084f, 0.07502372563f, -0.04592199996f, 0.0247429572f,
        -0.01101632882f, 0.002590026706f, 0.002369836438f, -0.005135977641f
    };
    chardsp::SimpleDownsampler<float, 4> downsampler;

    for (size_t i = 0; i < reference.size(); ++i)
        CHOC_EXPECT_NEAR (downsampler.process ({ i == 0 ? 1.0f : 0.0f,
                                                0.0f, 0.0f, 0.0f }),
                          reference[i], 0.000001f);

    downsampler.reset();
    CHOC_EXPECT_NEAR (downsampler.process ({ 1.0f, 0.0f, 0.0f, 0.0f }),
                      reference[0], 0.000001f);
}

float downsampledSineRMS (float frequency)
{
    constexpr float oversampledRate = 192000.0f;
    constexpr int discardedOutputs = 4096;
    constexpr int measuredOutputs = 8192;
    chardsp::SimpleDownsampler<float, 4> downsampler;
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

void testSimpleDownsamplerBehavior (choc::test::TestProgress& progress)
{
    chardsp::SimpleDownsampler<float, 2> two;
    chardsp::SimpleDownsampler<float, 4> four;
    chardsp::SimpleDownsampler<float, 8> eight;
    float dc2 = 0.0f;
    float dc4 = 0.0f;
    float dc8 = 0.0f;

    for (int i = 0; i < 4096; ++i)
    {
        dc2 = two.process ({ 1.0f, 1.0f });
        dc4 = four.process ({ 1.0f, 1.0f, 1.0f, 1.0f });
        dc8 = eight.process ({ 1.0f, 1.0f, 1.0f, 1.0f,
                               1.0f, 1.0f, 1.0f, 1.0f });
    }

    CHOC_EXPECT_NEAR (dc2, 1.0f, 0.00001f);
    CHOC_EXPECT_NEAR (dc4, 1.0f, 0.00001f);
    CHOC_EXPECT_NEAR (dc8, 1.0f, 0.00001f);

    const auto lowFrequencyRMS = downsampledSineRMS (1000.0f);
    const auto highFrequencyRMS = downsampledSineRMS (40000.0f);
    CHOC_EXPECT_TRUE (lowFrequencyRMS > 0.65f);
    CHOC_EXPECT_TRUE (highFrequencyRMS < lowFrequencyRMS * 0.1f);

    const auto allocationsBeforeRender = allocationCount.load();
    bool allFinite = true;

    for (int i = 0; i < 200000; ++i)
    {
        const auto input = std::sin (0.031f * static_cast<float> (i));
        allFinite = allFinite && std::isfinite (four.process (
            { input, -input, input * 0.5f, -input * 0.5f }));
    }

    CHOC_EXPECT_TRUE (allFinite);
    CHOC_EXPECT_EQ (allocationCount.load(), allocationsBeforeRender);
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
        decltype (std::declval<chardsp::PolyBLEPTriangleOscillator<double>&>()
                      .next()),
        double>);

    chardsp::SmoothedValue<double> smoother;
    smoother.reset (0.0);
    smoother.setTarget (1.0, 4);
    CHOC_EXPECT_TRUE (std::abs (smoother.next() - 0.25) < 1.0e-12);

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

    chardsp::SimpleDownsampler<double, 4> downsampler;
    chardsp::PolyBLEPTriangleOscillator<double> oscillator;
    oscillator.prepare (48000.0);
    oscillator.setFrequency (440.0);

    chardsp::EllipticSawOscillator<double> ellipticSaw;
    ellipticSaw.prepare (48000.0);
    ellipticSaw.setFrequency (440.0);

    chardsp::BasicSynthVoice<double> basicVoice;
    basicVoice.prepare (48000.0, 3);
    basicVoice.setOscillator1Waveform (chardsp::BasicSynthWaveform::pulse);
    basicVoice.setOscillator1PulseWidth (0.35);
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
            { filtered, filtered * 0.5, -filtered * 0.25,
              filtered * 0.125 });
        allFinite = allFinite && std::isfinite (downsampled)
                   && std::isfinite (ellipticSaw.next())
                   && std::isfinite (basicVoice.next());
    }

    CHOC_EXPECT_TRUE (allFinite);
    CHOC_EXPECT_EQ (allocationCount.load(), allocationsBeforeRender);

    noise.reset (77);
    const auto expectedNoise = noise.next();
    noise.reset (77);
    CHOC_EXPECT_EQ (noise.next(), expectedNoise);
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
        releaseSamplesRemaining = seconds > 0.0f
            ? std::max (1, static_cast<int> (seconds * sampleRate + 0.5f))
            : 0;
        if (releaseSamplesRemaining == 0)
            active = false;
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

void testVoicePoolBasics (choc::test::TestProgress& progress)
{
    using Pool = chardsp::VoicePool<TestVoice, 3>;
    Pool pool;
    pool.prepare (1000.0f);

    pool.next();
    CHOC_EXPECT_EQ (pool.getVoice (0).renderCount, 1);
    CHOC_EXPECT_EQ (pool.getVoice (1).renderCount, 1);
    CHOC_EXPECT_EQ (pool.getVoice (2).renderCount, 1);

    pool.start (60, 0.8f);
    CHOC_EXPECT_EQ (pool.findVoice (60), 0);
    CHOC_EXPECT_EQ (pool.getVoice (0).startCount, 1);
    CHOC_EXPECT_NEAR (pool.getVoice (0).velocity, 0.8f, 0.000001f);
    CHOC_EXPECT_EQ (pool.getActiveVoiceCount(), 1);

    pool.start (60, 0.4f);
    CHOC_EXPECT_EQ (pool.getVoice (0).startCount, 1);
    pool.start (62, 0.7f);
    pool.start (64, 0.6f);
    CHOC_EXPECT_EQ (pool.getActiveVoiceCount(), 3);
    CHOC_EXPECT_EQ (pool.findVoice (64), 2);
    CHOC_EXPECT_TRUE (pool.isKeyHeld (60));

    pool.stop (62);
    CHOC_EXPECT_EQ (pool.getVoice (1).releaseCount, 1);
    pool.start (62, 0.9f);
    CHOC_EXPECT_EQ (pool.getVoice (1).forcedReleaseCount, 1);
    CHOC_EXPECT_NEAR (pool.getVoice (1).lastForcedReleaseSeconds, 0.002f, 0.000001f);
    CHOC_EXPECT_EQ (pool.getVoice (1).startCount, 1);
    pool.next();
    CHOC_EXPECT_EQ (pool.getVoice (1).startCount, 1);
    pool.next();
    CHOC_EXPECT_EQ (pool.getVoice (1).startCount, 2);
    CHOC_EXPECT_EQ (pool.getVoice (1).lastRenderedStartCount, 1);
    pool.next();
    CHOC_EXPECT_EQ (pool.getVoice (1).lastRenderedStartCount, 2);
    CHOC_EXPECT_EQ (pool.findVoice (62), 1);

    // A sounding release tail stays reserved, so another key uses a free slot.
    pool.prepare (1000.0f);
    pool.start (60, 1.0f);
    pool.stop (60);

    for (int i = 0; i < 3; ++i)
        pool.next();

    CHOC_EXPECT_EQ (pool.getActiveVoiceCount(), 1);
    CHOC_EXPECT_EQ (pool.findVoice (60), 0);
    pool.start (62, 1.0f);
    CHOC_EXPECT_EQ (pool.findVoice (60), 0);
    CHOC_EXPECT_EQ (pool.findVoice (62), 1);

    // Unlike reference event-only allocator, the C++ pool can ask the voice when
    // its tail is actually available and reclaim it immediately.
    pool.prepare (1000.0f);
    pool.start (60, 1.0f);
    pool.stop (60);

    for (int i = 0; i < 3; ++i)
        pool.next();

    CHOC_EXPECT_EQ (pool.findVoice (60), 0);
    pool.next();
    CHOC_EXPECT_EQ (pool.findVoice (60), -1);

    // An active same-key release tail force-releases and restarts its existing
    // slot after exactly 2 ms.
    pool.prepare (1000.0f);
    pool.start (60, 1.0f);
    pool.stop (60);
    pool.start (60, 0.25f);
    CHOC_EXPECT_EQ (pool.getVoice (0).forcedReleaseCount, 1);
    pool.next();
    CHOC_EXPECT_EQ (pool.getVoice (0).startCount, 1);
    pool.next();
    CHOC_EXPECT_EQ (pool.getVoice (0).startCount, 2);
    CHOC_EXPECT_EQ (pool.getVoice (0).lastRenderedStartCount, 1);
    pool.next();
    CHOC_EXPECT_EQ (pool.getVoice (0).lastRenderedStartCount, 2);
    CHOC_EXPECT_NEAR (pool.getVoice (0).velocity, 0.25f, 0.000001f);

    // Releasing a pending key before its delayed restart cancels it.
    pool.stop (60);
    pool.start (60, 1.0f);
    pool.stop (60);
    pool.next();
    pool.next();
    CHOC_EXPECT_EQ (pool.getVoice (0).startCount, 2);
}

void testVoicePoolStealing (choc::test::TestProgress& progress)
{
    // Any stolen voice uses the same delayed fade, not only same-key
    // retriggers. The new voice starts after the old voice reports availability.
    using SingleVoicePool = chardsp::VoicePool<TestVoice, 1>;
    SingleVoicePool single;
    single.prepare (1000.0f);
    single.start (60, 1.0f);
    single.start (67, 0.5f);
    CHOC_EXPECT_TRUE (single.isPending (67));
    CHOC_EXPECT_EQ (single.findVoice (60), -1);
    CHOC_EXPECT_EQ (single.findVoice (67), 0);
    CHOC_EXPECT_EQ (single.getVoice (0).forcedReleaseCount, 1);
    CHOC_EXPECT_NEAR (single.getVoice (0).lastForcedReleaseSeconds, 0.002f, 0.000001f);
    single.next();
    CHOC_EXPECT_EQ (single.getVoice (0).startCount, 1);
    single.next();
    CHOC_EXPECT_EQ (single.getVoice (0).startCount, 2);
    CHOC_EXPECT_FALSE (single.isPending (67));
    CHOC_EXPECT_EQ (single.getVoice (0).lastRenderedStartCount, 1);
    single.next();
    CHOC_EXPECT_EQ (single.getVoice (0).lastRenderedStartCount, 2);

    // The steal fade is configurable, and zero selects immediate replacement.
    single.prepare (1000.0f);
    single.setStealFadeTime (0.004f);
    CHOC_EXPECT_NEAR (single.getStealFadeTime(), 0.004f, 0.000001f);
    single.start (60, 1.0f);
    single.start (62, 1.0f);

    for (int i = 0; i < 3; ++i)
    {
        single.next();
        CHOC_EXPECT_EQ (single.getVoice (0).startCount, 1);
    }

    single.next();
    CHOC_EXPECT_EQ (single.getVoice (0).startCount, 2);
    single.setStealFadeTime (0.0f);
    single.start (64, 1.0f);
    CHOC_EXPECT_EQ (single.getVoice (0).startCount, 3);
    CHOC_EXPECT_NEAR (single.getVoice (0).lastForcedReleaseSeconds, 0.0f, 0.000001f);
    single.setStealFadeTime (std::numeric_limits<float>::quiet_NaN());
    CHOC_EXPECT_NEAR (single.getStealFadeTime(), 0.0f, 0.000001f);

    // Each slot owns its pending start. Fast notes can therefore fade multiple
    // stolen voices concurrently without a shared timer or heap allocation.
    using PairPool = chardsp::VoicePool<TestVoice, 2>;
    PairPool pair;
    pair.prepare (1000.0f);
    pair.start (60, 1.0f);
    pair.start (62, 1.0f);
    pair.start (64, 1.0f);
    pair.start (65, 1.0f);
    CHOC_EXPECT_EQ (pair.findVoice (64), 0);
    CHOC_EXPECT_EQ (pair.findVoice (65), 1);
    CHOC_EXPECT_EQ (pair.getVoice (0).startCount, 1);
    CHOC_EXPECT_EQ (pair.getVoice (1).startCount, 1);
    pair.next();
    pair.next();
    CHOC_EXPECT_EQ (pair.getVoice (0).startCount, 2);
    CHOC_EXPECT_EQ (pair.getVoice (1).startCount, 2);

    // When every slot is already fading, the latest note replaces the oldest
    // pending start without restarting the old voice's fade.
    pair.prepare (1000.0f);
    pair.start (60, 1.0f);
    pair.start (62, 1.0f);
    pair.start (64, 1.0f);
    pair.start (65, 1.0f);
    pair.start (67, 1.0f);
    CHOC_EXPECT_EQ (pair.findVoice (64), -1);
    CHOC_EXPECT_EQ (pair.findVoice (67), 0);
    CHOC_EXPECT_EQ (pair.getVoice (0).forcedReleaseCount, 1);

    // A burst much larger than the voice count stays bounded. With two slots,
    // the two newest pending keys survive until the original voices become available.
    pair.prepare (1000.0f);
    pair.start (60, 1.0f);
    pair.start (62, 1.0f);

    for (int note = 64; note < 84; ++note)
        pair.start (note, 1.0f);

    CHOC_EXPECT_EQ (pair.getActiveVoiceCount(), 2);
    CHOC_EXPECT_EQ (pair.getAllocatedVoiceCount(), 2);
    CHOC_EXPECT_TRUE (pair.findVoice (82) >= 0);
    CHOC_EXPECT_TRUE (pair.findVoice (83) >= 0);

    for (int note = 64; note < 82; ++note)
        CHOC_EXPECT_EQ (pair.findVoice (note), -1);

    pair.next();
    pair.next();
    CHOC_EXPECT_TRUE ((pair.getVoice (0).note == 82
            && pair.getVoice (1).note == 83)
           || (pair.getVoice (0).note == 83
               && pair.getVoice (1).note == 82));
    CHOC_EXPECT_EQ (pair.getVoice (0).startCount, 2);
    CHOC_EXPECT_EQ (pair.getVoice (1).startCount, 2);

    // A note-off received while a note is pending cancels its future start.
    single.prepare (1000.0f);
    single.setStealFadeTime (0.002f);
    single.start (60, 1.0f);
    single.start (62, 1.0f);
    single.stop (62);
    single.next();
    single.next();
    CHOC_EXPECT_EQ (single.getVoice (0).startCount, 1);
    CHOC_EXPECT_EQ (single.findVoice (62), -1);
}

void testVoicePoolLifecycle (choc::test::TestProgress& progress)
{
    using Pool = chardsp::VoicePool<TestVoice, 3>;
    Pool pool;

    // The priority order remains oldest releasing, then oldest held.
    pool.prepare (1000.0f);
    pool.start (60, 1.0f);
    pool.start (62, 1.0f);
    pool.start (64, 1.0f);
    pool.stop (60);
    pool.stop (62);
    pool.start (65, 1.0f);
    CHOC_EXPECT_EQ (pool.findVoice (65), 0);
    CHOC_EXPECT_EQ (pool.findVoice (60), -1);
    CHOC_EXPECT_EQ (pool.findVoice (62), 1);

    // The oldest held voice is the final fallback. Additional fast notes can
    // reserve other slots while earlier stolen voices are still fading.
    pool.prepare (1000.0f);
    pool.start (60, 1.0f);
    pool.start (62, 1.0f);
    pool.start (64, 1.0f);
    pool.start (67, 1.0f);
    CHOC_EXPECT_EQ (pool.findVoice (67), 0);
    CHOC_EXPECT_EQ (pool.findVoice (60), -1);

    pool.stop (62);
    pool.start (62, 1.0f);
    pool.start (69, 1.0f);
    CHOC_EXPECT_TRUE (pool.findVoice (69) >= 0);

    pool.releaseAll();

    for (int i = 0; i < 4; ++i)
        pool.next();

    CHOC_EXPECT_EQ (pool.getActiveVoiceCount(), 0);

    pool.prepare (1000.0f);
    pool.start (69, 1.0f);
    pool.start (71, 1.0f);
    pool.reset();
    CHOC_EXPECT_EQ (pool.getVoice (0).forcedReleaseCount
           + pool.getVoice (1).forcedReleaseCount
           + pool.getVoice (2).forcedReleaseCount, 2);
    CHOC_EXPECT_EQ (pool.getActiveVoiceCount(), 0);
    CHOC_EXPECT_EQ (pool.getAllocatedVoiceCount(), 0);

    // Sustain defers voice release without introducing MIDI into the pool.
    pool.prepare (1000.0f);
    pool.start (72, 1.0f);
    pool.setSustain (true);
    pool.stop (72);
    CHOC_EXPECT_FALSE (pool.isKeyHeld (72));
    CHOC_EXPECT_EQ (pool.getVoice (0).releaseCount, 0);
    pool.setSustain (false);
    CHOC_EXPECT_EQ (pool.getVoice (0).releaseCount, 1);

    // Release duration comes from the Voice, not a guessed allocator timeout.
    // This tail deliberately lasts longer than reference old ten-second memory.
    pool.prepare (10.0f);
    pool.start (74, 1.0f);
    pool.stop (74);
    pool.getVoice (0).releaseSamplesRemaining = 102;

    for (int i = 0; i < 101; ++i)
        pool.next();

    CHOC_EXPECT_EQ (pool.findVoice (74), 0);
    pool.next();
    CHOC_EXPECT_EQ (pool.findVoice (74), -1);
}

void testVoicePoolKeysAndAllocations (choc::test::TestProgress& progress)
{
    // The key domain is caller-defined and has no MIDI or 32-key ceiling.
    // Only the four actual voices are stored, no matter how many source IDs
    // address the pool over time.
    using SourcePool = chardsp::VoicePool<SourceVoice, 4, SourceKey>;
    SourcePool sources;
    sources.prepare (1000.0f);

    for (uint64_t source = 0; source < 10000; ++source)
        sources.start ({ source, 100.0f + static_cast<float> (source % 900) },
                       0.5f);

    CHOC_EXPECT_EQ (sources.getAllocatedVoiceCount(), 4);
    CHOC_EXPECT_EQ (sources.getActiveVoiceCount(), 4);
    CHOC_EXPECT_TRUE (sources.findVoice ({ 9999, 0.0f }) >= 0);
    CHOC_EXPECT_EQ (sources.findVoice ({ 0, 0.0f }), -1);

    const auto latest = static_cast<size_t> (
        sources.findVoice ({ 9999, 0.0f }));
    const auto startsBeforeDuplicate = sources.getVoice (latest).startCount;
    sources.start ({ 9999, 777.0f }, 1.0f);
    CHOC_EXPECT_EQ (sources.getVoice (latest).startCount, startsBeforeDuplicate);
    sources.stop ({ 12, 0.0f });
    CHOC_EXPECT_EQ (sources.getActiveVoiceCount(), 4);

    chardsp::VoicePool<TestVoice, 3> pool;
    pool.prepare (1000.0f);
    const auto allocationsBeforeExercise = allocationCount.load();

    for (int event = 0; event < 100000; ++event)
    {
        pool.start (event, 0.75f);

        if (event % 3 == 0)
            pool.stop (event);

        pool.next();
    }

    CHOC_EXPECT_EQ (allocationCount.load(), allocationsBeforeExercise);
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

    CHOC_EXPECT_NEAR (glide.next(), 60.0f, 0.000001f);

    for (int i = 1; i < 50; ++i)
        glide.next();

    CHOC_EXPECT_NEAR (glide.next(), 66.0f, 0.000001f);

    for (int i = 51; i <= 100; ++i)
        glide.next();

    CHOC_EXPECT_NEAR (glide.next(), 72.0f, 0.000001f);
    CHOC_EXPECT_FALSE (glide.isGliding());

    // At full rate scaling, a half-octave takes half the configured time.
    glide.reset (60.0f);
    glide.setTimeRateMorph (1.0f);
    glide.setTarget (66.0f, true);

    for (int i = 0; i < 50; ++i)
        glide.next();

    CHOC_EXPECT_FALSE (glide.isGliding());
    CHOC_EXPECT_NEAR (glide.next(), 66.0f, 0.000001f);

    // The curved endpoint is the active exponential equation.
    glide.reset (60.0f);
    glide.setTimeRateMorph (0.0f);
    glide.setShape (1.0f);
    glide.setTarget (72.0f, true);

    for (int i = 0; i < 50; ++i)
        glide.next();

    const auto strength = 6.0f;
    const auto exponential = (1.0f - std::exp (-strength * 0.5f))
                           / (1.0f - std::exp (-strength));
    CHOC_EXPECT_NEAR (glide.next(), 60.0f + 12.0f * exponential, 0.00001f);

    glide.setTarget (48.0f, false);
    CHOC_EXPECT_NEAR (glide.next(), 48.0f, 0.000001f);
}

void testMonoVoiceController (choc::test::TestProgress& progress)
{
    using Controller = chardsp::MonoVoiceController<float, int, 8>;
    Controller mono;
    mono.prepare (1000.0f);
    mono.setLegato (true);
    mono.setPortamentoMode (chardsp::PortamentoMode::fingered);

    auto action = mono.start (1, 60.0f, 0.8f);
    CHOC_EXPECT_TRUE (action.hasPitch() && action.trigger && ! action.glide);
    action = mono.start (2, 64.0f, 0.7f);
    CHOC_EXPECT_TRUE (action.hasPitch() && ! action.trigger && action.glide);

    // Last-note priority falls back without retriggering the envelope.
    action = mono.stop (2);
    CHOC_EXPECT_TRUE (action.hasPitch());
    CHOC_EXPECT_NEAR (action.pitch, 60.0f, 0.000001f);
    CHOC_EXPECT_TRUE (! action.trigger && action.glide);

    mono.start (2, 64.0f, 0.7f);
    mono.start (3, 67.0f, 0.6f);
    action = mono.stopMatching (
        [] (int key) noexcept { return key >= 2; });
    CHOC_EXPECT_TRUE (action.hasPitch());
    CHOC_EXPECT_NEAR (action.pitch, 60.0f, 0.000001f);
    CHOC_EXPECT_EQ (mono.getHeldCount(), 1);

    // Stale note-offs and releases below the active note do not change pitch.
    mono.start (2, 64.0f, 0.7f);
    action = mono.stopMatching (
        [] (int key) noexcept { return key == 99; });
    CHOC_EXPECT_TRUE (! action.hasPitch() && ! action.shouldRelease());
    action = mono.stopMatching (
        [] (int key) noexcept { return key == 1; });
    CHOC_EXPECT_TRUE (! action.hasPitch() && ! action.shouldRelease());
    CHOC_EXPECT_EQ (mono.getHeldCount(), 1);

    // Legato delays the final gate-off by reference 24 ms grace window.
    CHOC_EXPECT_FALSE (mono.stop (2).shouldRelease());
    CHOC_EXPECT_TRUE (mono.hasPendingGateOff());

    for (int i = 0; i < 23; ++i)
        CHOC_EXPECT_FALSE (mono.next().shouldRelease());

    CHOC_EXPECT_TRUE (mono.next().shouldRelease());
    CHOC_EXPECT_FALSE (mono.isGateActive());

    // A new note in that grace window cancels the release and stays legato.
    mono.reset();
    mono.setLegato (true);
    mono.start (1, 60.0f, 1.0f);
    mono.stop (1);

    for (int i = 0; i < 10; ++i)
        mono.next();

    action = mono.start (2, 67.0f, 1.0f);
    CHOC_EXPECT_TRUE (action.hasPitch() && ! action.trigger);
    CHOC_EXPECT_FALSE (mono.hasPendingGateOff());

    // Always glides across a gap; fingered glides only while a key is held.
    mono.reset();
    mono.setLegato (false);
    mono.setPortamentoMode (chardsp::PortamentoMode::always);
    mono.start (1, 60.0f, 1.0f);
    CHOC_EXPECT_TRUE (mono.stop (1).shouldRelease());
    CHOC_EXPECT_TRUE (mono.start (2, 72.0f, 1.0f).glide);
    mono.stop (2);
    mono.setPortamentoMode (chardsp::PortamentoMode::fingered);
    CHOC_EXPECT_FALSE (mono.start (3, 67.0f, 1.0f).glide);

    // Sustain owns only the gate; releasing it ends a voice with no held key.
    mono.reset();
    mono.setSustain (true);
    mono.start (1, 60.0f, 1.0f);
    CHOC_EXPECT_FALSE (mono.stop (1).shouldRelease());
    CHOC_EXPECT_TRUE (mono.isGateActive());
    CHOC_EXPECT_TRUE (mono.setSustain (false).shouldRelease());

    // Capacity is explicit and fixed rather than a MIDI-sized hidden limit.
    chardsp::MonoVoiceController<double, uint64_t, 2> small;
    small.prepare (48000.0);
    small.start (1000, 60.0, 1.0);
    small.start (2000, 62.0, 1.0);
    small.start (3000, 64.0, 1.0);
    CHOC_EXPECT_EQ (small.getHeldCount(), 2);
    CHOC_EXPECT_TRUE (small.releaseAll().shouldRelease());
    CHOC_EXPECT_EQ (small.getHeldCount(), 0);
    CHOC_EXPECT_FALSE (small.isGateActive());
}

double decibels (double magnitude)
{
    return 20.0 * std::log10 (std::max (1.0e-8, magnitude));
}

void testConstantQBands (choc::test::TestProgress& progress)
{
    using chardsp::ConstantQ;
    CHOC_EXPECT_EQ (ConstantQ<double>::minimumBlockSize (48000.0, 24.0, 55.0),
                    32768);

    // Bands at A4 · 2^(k / bandsPerOctave), from minFrequency to maxFrequency.
    chardsp::ConstantQDesign design;
    design.sampleRate = 48000.0;
    design.bandsPerOctave = 24.0;
    design.minFrequency = 55.0;
    design.maxFrequency = 8000.0;
    ConstantQ<double> cq;
    CHOC_EXPECT_TRUE (cq.prepare (design));
    CHOC_EXPECT_EQ (cq.getBlockSize(), 32768);
    CHOC_EXPECT_EQ (cq.getNumBands(), 173); // k = -72 to 100: 55 Hz to 7.9 kHz

    for (int b = 0; b < cq.getNumBands(); ++b)
        CHOC_EXPECT_NEAR (cq.getFrequencies()[static_cast<size_t> (b)],
                          440.0 * std::exp2 ((b - 72) / 24.0), 1.0e-9);

    design.referenceFrequency = 432.0;
    design.bandsPerOctave = 12.0;
    CHOC_EXPECT_TRUE (cq.prepare (design));
    CHOC_EXPECT_EQ (cq.getNumBands(), 86); // k = -35 to 50: 57.2 Hz to 7.7 kHz
    CHOC_EXPECT_NEAR (cq.getFrequencies()[11], 108.0, 1.0e-9); // A2
    CHOC_EXPECT_NEAR (cq.getFrequencies()[35], 432.0, 1.0e-9); // A4

    // Fractional bands per octave.
    design.referenceFrequency = 440.0;
    design.bandsPerOctave = 18.5;
    CHOC_EXPECT_TRUE (cq.prepare (design));
    CHOC_EXPECT_NEAR (cq.getFrequencies()[1] / cq.getFrequencies()[0],
                      std::exp2 (1.0 / 18.5), 1.0e-12);

    // Designs it refuses: a band at Nyquist, a block that isn't a power of
    // two, one too short for the lowest band, no band in range.
    auto bad = design;
    bad.maxFrequency = 24000.0;
    CHOC_EXPECT_FALSE (cq.prepare (bad));
    CHOC_EXPECT_FALSE (cq.isValid());
    CHOC_EXPECT_EQ (cq.getNumBands(), 0);
    bad = design;
    bad.blockSize = 30000;
    CHOC_EXPECT_FALSE (cq.prepare (bad));
    bad.blockSize = 1024;
    CHOC_EXPECT_FALSE (cq.prepare (bad));
    bad = design;
    bad.minFrequency = 441.0;
    bad.maxFrequency = 442.0;
    CHOC_EXPECT_FALSE (cq.prepare (bad));
}

void testConstantQReadings (choc::test::TestProgress& progress)
{
    using chardsp::ConstantQReading;
    chardsp::ConstantQDesign design;
    design.sampleRate = 48000.0;
    design.bandsPerOctave = 24.0;
    design.minFrequency = 55.0;
    design.maxFrequency = 16000.0;
    chardsp::ConstantQ<double> cq;
    CHOC_EXPECT_TRUE (cq.prepare (design));
    const int size = cq.getBlockSize();
    const double middle = 0.5 * size;
    std::vector<double> block (static_cast<size_t> (size));

    // A full-scale sine on a band's centre reads 0 dB there, in every
    // reading, and less in the bands either side. (The lowest bands span so
    // few bins that the block's edges show: there it reads within 1 dB.)
    for (int b : { 0, 50, 120, cq.getNumBands() - 1 })
    {
        const double frequency = cq.getFrequencies()[static_cast<size_t> (b)];

        for (int i = 0; i < size; ++i)
            block[static_cast<size_t> (i)] = std::sin (
                6.283185307179586 * frequency * i / design.sampleRate + 0.4);

        cq.process (block.data());
        const double tolerance = b == 0 ? 1.0 : 0.01;
        CHOC_EXPECT_NEAR (decibels (cq.getCentreMagnitude (b)), 0.0, tolerance);

        for (auto reading : { ConstantQReading::peak, ConstantQReading::mean })
            CHOC_EXPECT_NEAR (decibels (cq.getMagnitude (
                b, reading, middle - 800.0, middle + 800.0)), 0.0, tolerance);

        if (b > 0)
            CHOC_EXPECT_TRUE (decibels (cq.getCentreMagnitude (b - 1)) < -0.5);

        if (b + 1 < cq.getNumBands())
            CHOC_EXPECT_TRUE (decibels (cq.getCentreMagnitude (b + 1)) < -0.5);
    }

    // The coefficients CiCueTea gives, and the same at any time.
    const int band = 100;
    std::vector<std::complex<double>> coefficients (
        static_cast<size_t> (cq.getNumCoefficients (band)));
    cq.getCoefficients (band, coefficients.data());
    const auto length = static_cast<int> (coefficients.size());
    CHOC_EXPECT_NEAR (std::abs (coefficients[static_cast<size_t> (length / 2)]),
                      cq.getCentreMagnitude (band), 1.0e-12);

    for (int n : { 0, 3, length / 2 + 1, length - 1 })
        CHOC_EXPECT_NEAR (std::abs (coefficients[static_cast<size_t> (n)]
                                    - cq.getCoefficient (
                                        band, static_cast<double> (n) * size / length)),
                          0.0, 1.0e-12);

    // Columns across the block: the steady sine (on the top band, the last
    // above) reads 0 dB in each.
    std::vector<double> columns (20);
    cq.getColumns (cq.getNumBands() - 1, ConstantQReading::mean, middle - 10000.0, 1000.0, 20,
                   columns.data());

    for (auto magnitude : columns)
        CHOC_EXPECT_NEAR (decibels (magnitude), 0.0, 0.01);

    // Float follows double.
    chardsp::ConstantQ<float> single;
    CHOC_EXPECT_TRUE (single.prepare (design));
    std::vector<float> floatBlock (block.begin(), block.end());
    single.process (floatBlock.data());
    CHOC_EXPECT_NEAR (decibels (single.getCentreMagnitude (cq.getNumBands() - 1)),
                      0.0, 0.01);

    // Silence: nothing in any band.
    std::fill (block.begin(), block.end(), 0.0);
    cq.process (block.data());

    for (int b = 0; b < cq.getNumBands(); ++b)
        CHOC_EXPECT_EQ (cq.getCentreMagnitude (b), 0.0);

    // Readings allocate nothing.
    const auto allocationsBefore = allocationCount.load();
    cq.process (block.data());

    for (int b = 0; b < cq.getNumBands(); ++b)
    {
        (void) cq.getMagnitude (b, ConstantQReading::peak, middle - 800.0,
                                middle + 800.0);
        (void) cq.getMagnitude (b, ConstantQReading::mean, middle - 800.0,
                                middle + 800.0);
        (void) cq.getCoefficient (b, 1234.5);
    }

    cq.getColumns (10, ConstantQReading::peak, 0.0, 512.0, 20, columns.data());
    cq.getCoefficients (band, coefficients.data());
    CHOC_EXPECT_EQ (allocationCount.load(), allocationsBefore);
}

void testConstantQColumns (choc::test::TestProgress& progress)
{
    using chardsp::ConstantQReading;
    chardsp::ConstantQDesign design;
    design.sampleRate = 48000.0;
    design.minFrequency = 55.0;
    design.maxFrequency = 60.0;

    // At the shortest block the lowest band reads a sine low, as its atom's
    // tails wrap round the block; in twice the block, not.
    design.bandsPerOctave = 24.0;
    chardsp::ConstantQ<double> cq;

    for (int times : { 1, 2 })
    {
        design.blockSize = times * chardsp::ConstantQ<double>::minimumBlockSize (
            48000.0, 24.0, 55.0);
        CHOC_EXPECT_TRUE (cq.prepare (design));
        std::vector<double> block (static_cast<size_t> (design.blockSize));

        for (size_t i = 0; i < block.size(); ++i)
            block[i] = std::sin (6.283185307179586 * 55.0 * i / 48000.0);

        cq.process (block.data());
        CHOC_EXPECT_NEAR (decibels (cq.getCentreMagnitude (0)),
                          times == 1 ? -0.73 : 0.0, times == 1 ? 0.01 : 0.001);
    }

    // Columns finer than a band's coefficients each read their own time: a
    // swelling sine in the lowest band of 96 per octave, whose coefficients
    // are 171 ms apart, read every 5 ms.
    design.bandsPerOctave = 96.0;
    design.blockSize = 0;
    CHOC_EXPECT_TRUE (cq.prepare (design));
    const int size = cq.getBlockSize();
    std::vector<double> block (static_cast<size_t> (size));

    for (size_t i = 0; i < block.size(); ++i)
        block[i] = static_cast<double> (i) / size * std::sin (
            6.283185307179586 * cq.getFrequencies()[0] * i / 48000.0);

    cq.process (block.data());
    const double first = 0.5 * size - 50 * 240.0;

    for (auto reading : { ConstantQReading::centre, ConstantQReading::mean })
    {
        std::vector<double> columns (100);
        cq.getColumns (0, reading, first, 240.0, 100, columns.data());

        for (size_t c = 0; c < columns.size(); ++c)
        {
            const double exact = std::abs (cq.getCoefficient (0, first + 240.0 * c));
            CHOC_EXPECT_NEAR (decibels (columns[c]), decibels (exact), 0.01);

            if (c > 0)
                CHOC_EXPECT_TRUE (columns[c] > columns[c - 1]);
        }
    }

    // A signal of any length in overlapping blocks reads as in one block
    // holding all of it, in any reading and at any spacing: within 1e-7 of
    // full scale (what the weights CiCueTea leaves out let in from the rest
    // of the block), and so within 0.001 dB above -60 dB.
    design.bandsPerOctave = 12.0;
    design.minFrequency = 110.0;
    design.maxFrequency = 4000.0;
    const int64_t length = 96000;
    std::vector<double> signal (static_cast<size_t> (length));
    uint32_t noise = 1;

    for (size_t i = 0; i < signal.size(); ++i)
    {
        noise = noise * 1664525u + 1013904223u;
        signal[i] = 0.3 * std::sin (0.015 * i + 2.0e-7 * i * i)
                  + 0.01 * (noise / 4294967296.0 - 0.5)
                  + (i % 20011 == 0 ? 1.0 : 0.0);
    }

    const int64_t start = 1 << 16; // on every band's coefficient grid
    design.blockSize = 1 << 18;
    chardsp::ConstantQ<double> whole;
    CHOC_EXPECT_TRUE (whole.prepare (design));
    std::vector<double> one (static_cast<size_t> (design.blockSize), 0.0);
    std::copy (signal.begin(), signal.end(), one.begin() + start);
    whole.process (one.data());
    const int bands = whole.getNumBands();

    design.blockSize = 4 * chardsp::ConstantQ<double>::minimumBlockSize (
        48000.0, 12.0, 110.0);
    chardsp::ConstantQ<double> parts;
    CHOC_EXPECT_TRUE (parts.prepare (design));

    for (auto reading : { ConstantQReading::centre, ConstantQReading::peak,
                          ConstantQReading::mean })
        for (double spacing : { 240.0, 1000.0, 37.5 })
        {
            const auto columns = static_cast<int64_t> (length / spacing);
            std::vector<double> expected (static_cast<size_t> (bands * columns));
            std::vector<double> stitched (expected.size());

            for (int b = 0; b < bands; ++b)
                whole.getColumns (b, reading, static_cast<double> (start), spacing,
                                  static_cast<int> (columns),
                                  expected.data() + b * columns);

            CHOC_EXPECT_TRUE (parts.getSignalColumns (signal.data(), length, reading,
                                                      spacing, columns, stitched.data()));
            double worst = 0.0, worstLoud = 0.0;

            for (size_t i = 0; i < expected.size(); ++i)
            {
                worst = std::max (worst, std::abs (stitched[i] - expected[i]));

                if (expected[i] > 1.0e-3)
                    worstLoud = std::max (worstLoud, std::abs (
                        decibels (stitched[i]) - decibels (expected[i])));
            }

            CHOC_EXPECT_TRUE (worst < 1.0e-7);
            CHOC_EXPECT_TRUE (worstLoud < 0.001);
        }

    // A block too short for its margins gives nothing; columns allocate
    // nothing.
    std::vector<double> columns (static_cast<size_t> (bands * 400));
    design.blockSize = 0;
    chardsp::ConstantQ<double> shortest;
    CHOC_EXPECT_TRUE (shortest.prepare (design));
    CHOC_EXPECT_FALSE (shortest.getSignalColumns (signal.data(), length,
        ConstantQReading::mean, 240.0, 400, columns.data()));
    const auto allocationsBefore = allocationCount.load();
    parts.getSignalColumns (signal.data(), length, ConstantQReading::mean, 240.0,
                            400, columns.data());
    CHOC_EXPECT_EQ (allocationCount.load(), allocationsBefore);
}

void testAnalysisBuffer (choc::test::TestProgress& progress)
{
    chardsp::AnalysisBuffer<double> buffer;
    CHOC_EXPECT_FALSE (buffer.prepare (0, 1024));
    CHOC_EXPECT_FALSE (buffer.prepare (2, 0));
    CHOC_EXPECT_TRUE (buffer.prepare (2, 1000));
    CHOC_EXPECT_EQ (buffer.getCapacity(), 1024);
    CHOC_EXPECT_EQ (buffer.getNumChannels(), 2);

    // Pushes `count` samples of each of `channels` channels: channel c's
    // sample at position n is n + 1000000 · c.
    std::vector<std::vector<double>> scratch (6, std::vector<double> (4096));
    uint32_t pushed = 0;
    const auto push = [&] (chardsp::AnalysisBuffer<double>& into, int channels,
                           int count)
    {
        const double* pointers[6];

        for (int c = 0; c < channels; ++c)
        {
            auto& samples = scratch[static_cast<size_t> (c)];

            for (int i = 0; i < count; ++i)
                samples[static_cast<size_t> (i)] = pushed + i + 1000000.0 * c;

            pointers[c] = samples.data();
        }

        into.push (pointers, channels, count);
        pushed += static_cast<uint32_t> (count);
    };

    // Reads what was pushed up to the hop's end, from every channel.
    std::vector<double> block (4096);
    const auto readsBack = [&] (const chardsp::AnalysisBuffer<double>& from,
                                int length)
    {
        bool same = true;

        for (int c = 0; c < from.getNumChannels(); ++c)
        {
            from.read (c, length, block.data());

            for (int i = 0; i < length; ++i)
            {
                const auto n = static_cast<int64_t> (from.getPosition())
                             - length + i;
                same = same && block[static_cast<size_t> (i)]
                    == (n < 0 ? 0.0 : static_cast<double> (n) + 1000000.0 * c);
            }
        }

        return same;
    };

    // Nothing until a block has arrived, then a hop at a time on its grid;
    // nothing at all when a block and a hop don't fit in the ring.
    buffer.restart (256);
    push (buffer, 2, 500);
    CHOC_EXPECT_FALSE (buffer.nextHop (512));
    push (buffer, 2, 100);
    CHOC_EXPECT_FALSE (buffer.nextHop (1024));
    int hops = 0;

    while (buffer.nextHop (512))
    {
        ++hops;
        CHOC_EXPECT_EQ (buffer.getPosition(), static_cast<uint32_t> (256 * hops));
        CHOC_EXPECT_EQ (buffer.getAdvanced(), 256u);
        CHOC_EXPECT_EQ (buffer.getSkipped(), 0);
        CHOC_EXPECT_TRUE (readsBack (buffer, 512));
    }

    CHOC_EXPECT_EQ (hops, 2);

    // Wraps around a small ring in uneven pushes, read back after each, from
    // 1, 2 and 6 channels.
    for (int channels : { 1, 2, 6 })
    {
        CHOC_EXPECT_TRUE (buffer.prepare (channels, 64));
        pushed = 0;
        buffer.restart (16);
        bool same = true;
        int count = 0;

        for (int round = 0; round < 200; ++round)
        {
            push (buffer, channels, 1 + (round * 7) % 13);

            while (buffer.nextHop (32))
                same = same && readsBack (buffer, 32)
                    && buffer.getPosition() % 16 == 0, ++count;
        }

        CHOC_EXPECT_TRUE (same);
        CHOC_EXPECT_EQ (count, static_cast<int> (pushed / 16));
    }

    // A reader that falls behind skips to the newest hops the ring still
    // holds (here two), and says how far it moved.
    CHOC_EXPECT_TRUE (buffer.prepare (1, 64));
    pushed = 0;
    buffer.restart (16);
    push (buffer, 1, 160);
    CHOC_EXPECT_TRUE (buffer.nextHop (32));
    CHOC_EXPECT_EQ (buffer.getSkipped(), 8);
    CHOC_EXPECT_EQ (buffer.getAdvanced(), 144u);
    CHOC_EXPECT_TRUE (readsBack (buffer, 32));
    CHOC_EXPECT_TRUE (buffer.nextHop (32));
    CHOC_EXPECT_EQ (buffer.getPosition(), 160u);
    CHOC_EXPECT_FALSE (buffer.nextHop (32));

    // Or past `keep` hops, to the newest `keep`.
    CHOC_EXPECT_TRUE (buffer.prepare (1, 8192));
    pushed = 0;
    buffer.restart (256);
    push (buffer, 1, 1024);
    uint32_t total = 0;
    hops = 0;

    while (buffer.nextHop (1024, 100))
        total += buffer.getAdvanced(), ++hops;

    CHOC_EXPECT_EQ (hops, 4);
    push (buffer, 1, 256 * 20);
    total = 0, hops = 0;

    while (buffer.nextHop (1024, 4))
        total += buffer.getAdvanced(), ++hops;

    CHOC_EXPECT_EQ (hops, 4);
    CHOC_EXPECT_EQ (total, 256u * 20);

    // Restarting from a position starts the grid there; channels not pushed
    // read as silence.
    CHOC_EXPECT_TRUE (buffer.prepare (3, 1024));
    pushed = 0;
    push (buffer, 3, 100);
    buffer.restart (10, 95);
    push (buffer, 1, 10);
    CHOC_EXPECT_TRUE (buffer.nextHop (20));
    CHOC_EXPECT_EQ (buffer.getPosition(), 105u);
    CHOC_EXPECT_EQ (buffer.getAdvanced(), 10u);
    CHOC_EXPECT_FALSE (buffer.nextHop (20));
    buffer.read (2, 10, block.data());
    CHOC_EXPECT_EQ (block[0], 1000000.0 * 2 + 95.0);
    CHOC_EXPECT_EQ (block[9], 0.0);

    // Pushing and reading allocate nothing.
    const auto allocationsBefore = allocationCount.load();
    push (buffer, 3, 500);

    while (buffer.nextHop (20))
        buffer.read (1, 20, block.data());

    CHOC_EXPECT_EQ (allocationCount.load(), allocationsBefore);
}

void testConstantQStream (choc::test::TestProgress& progress)
{
    using chardsp::ConstantQReading;
    chardsp::ConstantQDesign design;
    design.sampleRate = 48000.0;
    design.bandsPerOctave = 12.0;
    design.minFrequency = 55.0;
    design.maxFrequency = 16000.0;
    const int hop = 1600;
    const int size = chardsp::ConstantQ<float>::minimumBlockSize (48000.0, 12.0, 55.0);

    // The levels a stream shows for each of `inputs` (a channel each): every
    // column, as columns[channel][band].
    using Columns = std::vector<std::vector<float>>;
    const auto runAll = [&] (const std::vector<std::vector<float>>& inputs,
                             ConstantQReading reading)
    {
        const auto channels = static_cast<int> (inputs.size());
        chardsp::AnalysisBuffer<float> buffer;
        buffer.prepare (channels, 2 * size);
        buffer.restart (hop);
        chardsp::ConstantQStream<float> stream;
        CHOC_EXPECT_TRUE (stream.configure (design, hop, reading));
        CHOC_EXPECT_EQ (stream.getBlockSize(), size);
        const auto bands = static_cast<size_t> (stream.getTransform().getNumBands());
        std::vector<Columns> columns (inputs.size(), Columns (bands));
        std::vector<float> block (static_cast<size_t> (size)), column (bands);
        std::vector<const float*> pointers (inputs.size());
        uint32_t total = 0;

        for (size_t i = 0; i + 256 <= inputs[0].size(); i += 256)
        {
            for (size_t c = 0; c < inputs.size(); ++c)
                pointers[c] = inputs[c].data() + i;

            buffer.push (pointers.data(), channels, 256);

            while (buffer.nextHop (size, 1000))
            {
                total += buffer.getAdvanced();

                for (int c = 0; c < channels; ++c)
                {
                    buffer.read (c, size, block.data());
                    stream.process (block.data(), column.data());

                    for (size_t b = 0; b < bands; ++b)
                        columns[static_cast<size_t> (c)][b].push_back (column[b]);
                }
            }
        }

        CHOC_EXPECT_EQ (total, static_cast<uint32_t> (inputs[0].size() / hop * hop));
        return columns;
    };
    const auto run = [&] (const std::vector<float>& input, ConstantQReading reading)
    {
        return runAll ({ input }, reading)[0];
    };

    chardsp::ConstantQ<float> bands;
    bands.prepare (design);
    const auto nearest = [&] (double frequency)
    {
        size_t best = 0;

        for (size_t b = 0; b < bands.getFrequencies().size(); ++b)
            if (std::abs (std::log (bands.getFrequencies()[b] / frequency))
                < std::abs (std::log (bands.getFrequencies()[best] / frequency)))
                best = b;

        return best;
    };

    // Silence reads the floor.
    const std::vector<float> silence (static_cast<size_t> (size + 10 * hop), 0.0f);

    for (auto& band : run (silence, ConstantQReading::mean))
        CHOC_EXPECT_EQ (band.back(), -160.0f);

    // A steady sine reads the same in all three.
    const auto sineAt = [&] (double frequency)
    {
        std::vector<float> sine (silence.size());

        for (size_t i = 0; i < sine.size(); ++i)
            sine[i] = static_cast<float> (std::sin (6.283185307179586 * frequency * i / 48000.0));

        return sine;
    };
    const auto sine = sineAt (4000.0);
    const auto band = nearest (4000.0);
    const auto centre = run (sine, ConstantQReading::centre)[band].back();
    CHOC_EXPECT_NEAR (run (sine, ConstantQReading::peak)[band].back(), centre, 0.01f);
    CHOC_EXPECT_NEAR (run (sine, ConstantQReading::mean)[band].back(), centre, 0.01f);

    // A click halfway between two columns' centres (column k is centred
    // size / 2 before k · hop): centre misses it at 12 kHz, peak and mean
    // don't.
    std::vector<float> click (silence.size(), 0.0f);
    click[static_cast<size_t> (8 * hop - size / 2 + hop / 2)] = 1.0f;
    const auto high = nearest (12000.0);
    const auto loudest = [&] (ConstantQReading reading)
    {
        const auto levels = run (click, reading)[high];
        return *std::max_element (levels.begin(), levels.end());
    };
    CHOC_EXPECT_TRUE (loudest (ConstantQReading::centre) < -120.0f);
    CHOC_EXPECT_TRUE (loudest (ConstantQReading::peak) > -40.0f);
    CHOC_EXPECT_TRUE (loudest (ConstantQReading::mean) > -60.0f);

    // Channels are analysed apart: each of six reads its own sine, as one
    // channel alone does, and nothing at the others'.
    std::vector<std::vector<float>> six;

    for (double frequency : { 110.0, 220.0, 440.0, 880.0, 1760.0, 3520.0 })
        six.push_back (sineAt (frequency));

    const auto sixColumns = runAll (six, ConstantQReading::mean);
    const auto twoColumns = runAll ({ six[0], six[3] }, ConstantQReading::mean);
    const auto alone = run (six[3], ConstantQReading::mean);

    for (size_t c = 0; c < six.size(); ++c)
        for (size_t other = 0; other < six.size(); ++other)
        {
            const auto level = sixColumns[c][nearest (110.0 * std::exp2 (
                static_cast<double> (other)))].back();

            if (other == c)
            {
                CHOC_EXPECT_NEAR (level, 0.0f, 0.1f);
            }
            else
            {
                CHOC_EXPECT_TRUE (level < -40.0f);
            }
        }

    CHOC_EXPECT_TRUE (twoColumns[1] == alone && sixColumns[3] == alone);
    CHOC_EXPECT_TRUE (twoColumns[0] == sixColumns[0]);

    // Columns allocate nothing.
    chardsp::AnalysisBuffer<float> buffer;
    buffer.prepare (1, 2 * size);
    buffer.restart (hop);
    chardsp::ConstantQStream<float> stream;
    stream.configure (design, hop, ConstantQReading::mean);
    std::vector<float> block (static_cast<size_t> (size));
    std::vector<float> column (static_cast<size_t> (stream.getTransform().getNumBands()));
    const auto allocationsBefore = allocationCount.load();

    for (size_t i = 0; i + 256 <= sine.size(); i += 256)
    {
        const float* channel = sine.data() + i;
        buffer.push (&channel, 1, 256);

        while (buffer.nextHop (size))
        {
            buffer.read (0, size, block.data());
            stream.process (block.data(), column.data());
        }
    }

    CHOC_EXPECT_EQ (allocationCount.load(), allocationsBefore);
}

void testSpectrum (choc::test::TestProgress& progress)
{
    using chardsp::SpectrumWindow;
    const auto sine = [] (std::vector<double>& block, double cycles,
                          double amplitude)
    {
        for (size_t i = 0; i < block.size(); ++i)
            block[i] = amplitude * std::sin (6.283185307179586 * cycles * i
                                             / block.size() + 0.3);
    };

    // A sine on a bin's centre reads its amplitude there, with every window,
    // symmetric or periodic, padded or not.
    chardsp::Spectrum<double> spectrum;
    std::vector<double> block;

    for (int size : { 256, 2048, 16384 })
        for (auto window : { SpectrumWindow::rectangular, SpectrumWindow::hann,
                             SpectrumWindow::hamming, SpectrumWindow::blackman,
                             SpectrumWindow::blackmanHarris })
            for (bool periodic : { false, true })
                for (int padding : { 1, 2 })
                {
                    chardsp::SpectrumDesign design;
                    design.size = size;
                    design.fftSize = size * padding;
                    design.window = window;
                    design.periodic = periodic;
                    CHOC_EXPECT_TRUE (spectrum.prepare (design));
                    CHOC_EXPECT_EQ (spectrum.getNumBins(), size * padding / 2 + 1);
                    block.resize (static_cast<size_t> (size));
                    sine (block, size / 8, 0.5);
                    spectrum.process (block.data());
                    CHOC_EXPECT_NEAR (decibels (spectrum.getMagnitude (
                        size * padding / 8)), decibels (0.5), 0.05);
                }

    // A constant reads its value at DC; silence reads nothing anywhere.
    chardsp::SpectrumDesign design;
    design.size = 1024;
    CHOC_EXPECT_TRUE (spectrum.prepare (design));
    CHOC_EXPECT_EQ (spectrum.getDesign().fftSize, 1024);
    block.assign (1024, 0.25);
    spectrum.process (block.data());
    CHOC_EXPECT_NEAR (spectrum.getMagnitude (0), 0.25, 1.0e-12);
    std::fill (block.begin(), block.end(), 0.0);
    spectrum.process (block.data());
    std::vector<double> magnitudes (513);
    spectrum.getMagnitudes (magnitudes.data());
    CHOC_EXPECT_TRUE (*std::max_element (magnitudes.begin(), magnitudes.end()) == 0.0);

    // Time runs from the block's middle: a cosine peaking there is real.
    design.periodic = true;
    spectrum.prepare (design);
    for (size_t i = 0; i < block.size(); ++i)
        block[i] = std::cos (6.283185307179586 * 40.0
                             * (static_cast<double> (i) - 512.0) / 1024.0);

    spectrum.process (block.data());
    CHOC_EXPECT_NEAR (spectrum.getBins()[40].imag(), 0.0, 1.0e-9);
    CHOC_EXPECT_TRUE (spectrum.getBins()[40].real() > 0.0);
    CHOC_EXPECT_NEAR (spectrum.getBins()[40].real() * spectrum.getScale(), 1.0, 1.0e-9);

    // Designs it refuses.
    design.periodic = false;
    design.fftSize = 512;
    CHOC_EXPECT_FALSE (spectrum.prepare (design));
    CHOC_EXPECT_FALSE (spectrum.isValid());
    design.fftSize = 1536;
    CHOC_EXPECT_FALSE (spectrum.prepare (design));
    design.size = 2;
    design.fftSize = 0;
    CHOC_EXPECT_FALSE (spectrum.prepare (design));

    // A spectrogram: the newest block of each channel every hop, through an
    // AnalysisBuffer, from 1, 2 and 6 channels, each a sine on its own bin.
    for (int channels : { 1, 2, 6 })
    {
        chardsp::SpectrumDesign live;
        live.size = 1024;
        chardsp::Spectrum<float> analysis;
        CHOC_EXPECT_TRUE (analysis.prepare (live));
        chardsp::AnalysisBuffer<float> buffer;
        buffer.prepare (channels, 4096);
        buffer.restart (256);
        std::vector<std::vector<float>> input (6, std::vector<float> (128));
        std::vector<const float*> pointers;

        for (auto& channel : input)
            pointers.push_back (channel.data());

        std::vector<float> samples (1024), column (513);
        int columns = 0;
        bool own = true;

        for (int n = 0; n < 8192; n += 128)
        {
            for (int c = 0; c < channels; ++c)
                for (int i = 0; i < 128; ++i)
                    input[static_cast<size_t> (c)][static_cast<size_t> (i)] =
                        static_cast<float> (std::sin (6.283185307179586
                            * (32 + 16 * c) * (n + i) / 1024.0));

            buffer.push (pointers.data(), channels, 128);

            while (buffer.nextHop (1024))
            {
                ++columns;

                for (int c = 0; c < channels; ++c)
                {
                    buffer.read (c, 1024, samples.data());
                    analysis.process (samples.data());
                    analysis.getMagnitudes (column.data());
                    const auto loudest = std::max_element (column.begin(), column.end());
                    // (The first blocks reach back before the sines.)
                    own = own && (buffer.getPosition() < 1024
                        || (loudest - column.begin() == 32 + 16 * c
                            && std::abs (decibels (*loudest)) < 0.05));
                }
            }
        }

        CHOC_EXPECT_EQ (columns, 32);
        CHOC_EXPECT_TRUE (own);
    }

    // Float follows double; processing and reading allocate nothing.
    chardsp::Spectrum<float> single;
    design.size = 2048;
    design.fftSize = 4096;
    design.window = SpectrumWindow::blackmanHarris;
    design.periodic = true;
    CHOC_EXPECT_TRUE (single.prepare (design));
    CHOC_EXPECT_TRUE (spectrum.prepare (design));
    block.resize (2048);
    sine (block, 100.25, 1.0);
    std::vector<float> floatBlock (block.begin(), block.end());
    std::vector<float> floatMagnitudes (2049);
    magnitudes.resize (2049);
    const auto allocationsBefore = allocationCount.load();
    single.process (floatBlock.data());
    single.getMagnitudes (floatMagnitudes.data());
    spectrum.process (block.data());
    spectrum.getMagnitudes (magnitudes.data());
    CHOC_EXPECT_EQ (allocationCount.load(), allocationsBefore);

    for (size_t k = 190; k < 211; ++k)
        CHOC_EXPECT_NEAR (floatMagnitudes[k], magnitudes[k], 1.0e-5);
}

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
    { CHOC_TEST (delay_line_and_crossfade); testDelayLineAndCrossfade (progress); }
    { CHOC_TEST (fractional_delay_line); testFractionalDelayLine (progress); }
    { CHOC_TEST (allpass_diffuser); testAllpassDiffuser (progress); }
    { CHOC_TEST (one_pole); testOnePole (progress); }
    { CHOC_TEST (dc_blocker_and_detectors); testDCBlockerAndDetectors (progress); }
    { CHOC_TEST (biquad); testBiquad (progress); }
    { CHOC_TEST (sample_player); testSamplePlayer (progress); }
    { CHOC_TEST (signalsmith_oversampler); testOversampler (progress); }
    { CHOC_TEST (elliptic_saw); testEllipticSaw (progress); }
    { CHOC_TEST (oscillator_phase_and_elliptic_correction);
      testOscillatorPhaseAndEllipticCorrection (progress); }
    { CHOC_TEST (generic_oscillators); testGenericOscillators (progress); }
    { CHOC_TEST (basic_synth_voice); testBasicSynthVoice (progress); }
    { CHOC_TEST (adsr_reference); testADSREnvelopeParity (progress); }
    { CHOC_TEST (adsr_behavior); testADSREnvelopeBehavior (progress); }
    { CHOC_TEST (adsr_live_updates); testADSREnvelopeLiveUpdates (progress); }
    { CHOC_TEST (svf_reference); testStateVariableFilterParity (progress); }
    { CHOC_TEST (svf_behavior); testStateVariableFilterBehavior (progress); }
    { CHOC_TEST (colored_noise_reference); testColoredNoiseParity (progress); }
    { CHOC_TEST (colored_noise_behavior); testColoredNoiseBehavior (progress); }
    { CHOC_TEST (analysis_buffer); testAnalysisBuffer (progress); }
    { CHOC_TEST (constant_q_bands); testConstantQBands (progress); }
    { CHOC_TEST (constant_q_readings); testConstantQReadings (progress); }
    { CHOC_TEST (constant_q_columns); testConstantQColumns (progress); }
    { CHOC_TEST (constant_q_stream); testConstantQStream (progress); }
    { CHOC_TEST (spectrum); testSpectrum (progress); }
    { CHOC_TEST (simple_downsampler_reference);
      testSimpleDownsamplerParity (progress); }
    { CHOC_TEST (simple_downsampler_behavior);
      testSimpleDownsamplerBehavior (progress); }
    { CHOC_TEST (double_precision_types); testDoublePrecisionTypes (progress); }
    { CHOC_TEST (voice_pool_basics); testVoicePoolBasics (progress); }
    { CHOC_TEST (voice_pool_stealing); testVoicePoolStealing (progress); }
    { CHOC_TEST (voice_pool_lifecycle); testVoicePoolLifecycle (progress); }
    { CHOC_TEST (voice_pool_keys_and_allocations);
      testVoicePoolKeysAndAllocations (progress); }
    { CHOC_TEST (portamento); testPortamento (progress); }
    { CHOC_TEST (mono_voice_controller); testMonoVoiceController (progress); }

    progress.printReport();
    return progress.numFails == 0 ? 0 : 1;
}
