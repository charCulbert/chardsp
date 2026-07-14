#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>

#include "chardsp/chardsp_EllipticSawOscillator.h"
#include "chardsp/chardsp_SmoothedValue.h"
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

    if (context.failures == 0)
    {
        std::cout << "chardsp tests: all checks passed\n";
        return 0;
    }

    std::cerr << "chardsp tests: " << context.failures << " checks failed\n";
    return 1;
}
