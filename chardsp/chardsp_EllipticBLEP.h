#pragma once

#include "chardsp/chardsp_OpenSourceLicenses.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <type_traits>

#include "chardsp/detail/chardsp_EllipticBLEPLicense.h"
#include "choc/platform/choc_DisableAllWarnings.h"
#include "elliptic-blep.h"
#include "choc/platform/choc_ReenableAllWarnings.h"

namespace chardsp
{

// Shared correction state for oscillators which schedule their own
// discontinuities. eventOffset is measured from the beginning of the current
// sample: 0 is the previous sample boundary and 1 is the current boundary.
//
// prepare() builds Signalsmith's lookup tables and is not an audio-thread
// operation. Every other method is allocation-free.
template <typename SampleType>
struct EllipticBLEPCorrection
{
    static_assert (std::is_same_v<SampleType, float>
                   || std::is_same_v<SampleType, double>,
                   "EllipticBLEPCorrection supports float or double");

    void prepare (SampleType sampleRate) noexcept
    {
        if (std::isfinite (sampleRate) && sampleRate > SampleType {})
            blep.emplace (sampleRate);
        else
            blep.reset();

        allpass.reset();
    }

    void reset() noexcept
    {
        if (blep)
            blep->reset();

        allpass.reset();
    }

    bool isPrepared() const noexcept { return blep.has_value(); }

    void beginSample() noexcept
    {
        if (blep)
            blep->step();
    }

    void addStep (SampleType eventOffset, SampleType height) noexcept
    {
        add (eventOffset, height, 1);
    }

    void addSlopeJump (SampleType eventOffset,
                       SampleType jumpPerSample) noexcept
    {
        add (eventOffset, jumpPerSample, 2);
    }

    void addEvent (SampleType eventOffset, SampleType stepHeight,
                   SampleType slopeJumpPerSample) noexcept
    {
        addStep (eventOffset, stepHeight);
        addSlopeJump (eventOffset, slopeJumpPerSample);
    }

    SampleType process (SampleType uncorrectedSample) noexcept
    {
        if (! blep)
            return uncorrectedSample;

        return allpass (uncorrectedSample + blep->get());
    }

private:
    void add (SampleType eventOffset, SampleType amount,
              std::size_t order) noexcept
    {
        if (! blep || ! std::isfinite (eventOffset)
            || ! std::isfinite (amount) || amount == SampleType {})
            return;

        const auto offset = std::clamp (
            eventOffset, SampleType {}, static_cast<SampleType> (1));
        blep->add (amount, order,
                   static_cast<SampleType> (1) - offset);
    }

    std::optional<signalsmith::blep::EllipticBlep<SampleType>> blep;
    signalsmith::blep::EllipticBlepAllpass<SampleType> allpass;
};

} // namespace chardsp
