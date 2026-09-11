#pragma once

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace chardsp
{

enum class OnePoleMode
{
    lowpass,
    highpass
};

// First-order lowpass or highpass sharing one state. The cutoff is the 1/e pole
// frequency, clamped to at least 1 Hz and kept below Nyquist; the highpass is
// the input minus the lowpass, so a mode change reuses the settled state.
template <typename SampleType>
struct OnePole
{
    static_assert (std::is_same_v<SampleType, float>
                   || std::is_same_v<SampleType, double>,
                   "OnePole supports float or double");

    OnePole() noexcept { setCutoff (cutoff); }

    void prepare (SampleType newSampleRate) noexcept
    {
        sampleRate = std::isfinite (newSampleRate)
                && newSampleRate > SampleType {}
            ? newSampleRate : static_cast<SampleType> (48000);
        setCutoff (cutoff);
        reset();
    }

    void reset() noexcept { state = SampleType {}; }

    void setMode (OnePoleMode newMode) noexcept { mode = newMode; }
    OnePoleMode getMode() const noexcept { return mode; }

    void setCutoff (SampleType cutoffHz) noexcept
    {
        const auto highest = std::max (static_cast<SampleType> (1),
            sampleRate * static_cast<SampleType> (0.49));
        cutoff = std::isfinite (cutoffHz)
            ? std::clamp (cutoffHz, static_cast<SampleType> (1), highest)
            : static_cast<SampleType> (1000);
        coefficient = static_cast<SampleType> (1)
            - std::exp (static_cast<SampleType> (-6.28318530717958647692)
                        * cutoff / sampleRate);
    }

    SampleType getCutoff() const noexcept { return cutoff; }

    SampleType process (SampleType input) noexcept
    {
        input = std::isfinite (input) ? input : SampleType {};
        state += (input - state) * coefficient;
        state = std::abs (state) < static_cast<SampleType> (1.0e-20)
            ? SampleType {} : state;
        return mode == OnePoleMode::lowpass ? state : input - state;
    }

private:
    SampleType sampleRate = static_cast<SampleType> (48000);
    SampleType cutoff = static_cast<SampleType> (1000);
    SampleType coefficient = SampleType {};
    SampleType state = SampleType {};
    OnePoleMode mode = OnePoleMode::lowpass;
};

} // namespace chardsp
