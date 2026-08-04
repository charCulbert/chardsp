#pragma once

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace chardsp
{

template <typename SampleType>
struct PulseEdgeEvent
{
    SampleType offset {};
    SampleType step {};
};

// Reports every discontinuity of a pulse whose phase and width both move
// linearly over one interval. Phase may be unwrapped, but the interval must
// span less than one cycle. Offsets are mapped into [offsetStart, offsetEnd].
//
// The callback is invoked chronologically at most three times and no storage
// is allocated.
template <typename SampleType, typename Callback>
void forEachMovingPulseEdge (
    SampleType phaseStart,
    SampleType phaseEnd,
    SampleType pulseWidthStart,
    SampleType pulseWidthEnd,
    SampleType offsetStart,
    SampleType offsetEnd,
    Callback&& callback) noexcept
{
    static_assert (
        std::is_same_v<SampleType, float>
        || std::is_same_v<SampleType, double>,
        "Pulse edge scheduling supports float or double");

    if (! std::isfinite (phaseStart)
        || ! std::isfinite (phaseEnd)
        || ! std::isfinite (pulseWidthStart)
        || ! std::isfinite (pulseWidthEnd)
        || ! std::isfinite (offsetStart)
        || ! std::isfinite (offsetEnd)
        || phaseEnd <= phaseStart
        || phaseEnd - phaseStart >= static_cast<SampleType> (1)
        || offsetEnd < offsetStart)
        return;

    const auto zero = SampleType {};
    const auto one = static_cast<SampleType> (1);
    const auto two = static_cast<SampleType> (2);
    const auto widthStart = std::clamp (pulseWidthStart, zero, one);
    const auto widthEnd = std::clamp (pulseWidthEnd, zero, one);
    const auto phaseTravel = phaseEnd - phaseStart;
    const auto offsetTravel = offsetEnd - offsetStart;
    const auto relativeSpeed =
        phaseTravel - (widthEnd - widthStart);
    const auto baseCycle = std::floor (phaseStart);

    const auto hasRelativeMotion = std::abs (relativeSpeed)
        > static_cast<SampleType> (1.0e-12);
    const auto emitPulseEdge = [&] (SampleType cycle)
    {
        if (! hasRelativeMotion)
            return;

        const auto pulseEdge = cycle + widthStart;
        const auto progress =
            (pulseEdge - phaseStart) / relativeSpeed;
        if (progress > zero && progress <= one)
            callback (
                PulseEdgeEvent<SampleType> {
                    offsetStart + progress * offsetTravel,
                    relativeSpeed > zero ? -two : two
                });
    };

    // Less than one phase cycle means only the current and next pulse edges
    // can intersect this interval. The wrap lies between those two edges.
    emitPulseEdge (baseCycle);
    const auto wrap = baseCycle + one;
    if (wrap > phaseStart && wrap <= phaseEnd)
    {
        const auto progress =
            (wrap - phaseStart) / phaseTravel;
        callback (
            PulseEdgeEvent<SampleType> {
                offsetStart + progress * offsetTravel,
                two
            });
    }
    emitPulseEdge (baseCycle + one);
}

} // namespace chardsp
