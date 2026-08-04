#pragma once

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace chardsp
{

// Coefficients use a0-normalised form:
// y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2]
//                    - a1*y[n-1] - a2*y[n-2].
template <typename SampleType>
struct BiquadCoefficients
{
    static_assert (std::is_same_v<SampleType, float>
                   || std::is_same_v<SampleType, double>,
                   "BiquadCoefficients supports float or double");

    SampleType b0 = static_cast<SampleType> (1);
    SampleType b1 = SampleType {};
    SampleType b2 = SampleType {};
    SampleType a1 = SampleType {};
    SampleType a2 = SampleType {};

    bool isStable() const noexcept
    {
        return allFinite()
            && std::abs (a2) < static_cast<SampleType> (1)
            && static_cast<SampleType> (1) + a1 + a2 > SampleType {}
            && static_cast<SampleType> (1) - a1 + a2 > SampleType {};
    }

    static BiquadCoefficients lowpass (SampleType sampleRate,
                                       SampleType frequency,
                                       SampleType q) noexcept
    {
        return designResonant (sampleRate, frequency, q,
            [] (auto cosine, auto alpha)
            {
                const auto b = (static_cast<SampleType> (1) - cosine)
                             * static_cast<SampleType> (0.5);
                return Raw { b, static_cast<SampleType> (2) * b, b,
                             static_cast<SampleType> (1) + alpha,
                             static_cast<SampleType> (-2) * cosine,
                             static_cast<SampleType> (1) - alpha };
            });
    }

    static BiquadCoefficients highpass (SampleType sampleRate,
                                        SampleType frequency,
                                        SampleType q) noexcept
    {
        return designResonant (sampleRate, frequency, q,
            [] (auto cosine, auto alpha)
            {
                const auto b = (static_cast<SampleType> (1) + cosine)
                             * static_cast<SampleType> (0.5);
                return Raw { b, static_cast<SampleType> (-2) * b, b,
                             static_cast<SampleType> (1) + alpha,
                             static_cast<SampleType> (-2) * cosine,
                             static_cast<SampleType> (1) - alpha };
            });
    }

    static BiquadCoefficients bandpass (SampleType sampleRate,
                                        SampleType frequency,
                                        SampleType q) noexcept
    {
        return designResonant (sampleRate, frequency, q,
            [] (auto cosine, auto alpha)
            {
                return Raw { alpha, SampleType {}, -alpha,
                             static_cast<SampleType> (1) + alpha,
                             static_cast<SampleType> (-2) * cosine,
                             static_cast<SampleType> (1) - alpha };
            });
    }

    static BiquadCoefficients notch (SampleType sampleRate,
                                     SampleType frequency,
                                     SampleType q) noexcept
    {
        return designResonant (sampleRate, frequency, q,
            [] (auto cosine, auto alpha)
            {
                return Raw { static_cast<SampleType> (1),
                             static_cast<SampleType> (-2) * cosine,
                             static_cast<SampleType> (1),
                             static_cast<SampleType> (1) + alpha,
                             static_cast<SampleType> (-2) * cosine,
                             static_cast<SampleType> (1) - alpha };
            });
    }

    static BiquadCoefficients peak (SampleType sampleRate,
                                    SampleType frequency,
                                    SampleType q,
                                    SampleType gainDB) noexcept
    {
        SampleType sine, cosine;

        if (! prepareFrequency (sampleRate, frequency, sine, cosine)
            || ! std::isfinite (q) || q <= SampleType {}
            || ! std::isfinite (gainDB))
            return {};

        const auto alpha = sine / (static_cast<SampleType> (2) * q);
        const auto amplitude = std::pow (static_cast<SampleType> (10),
            std::clamp (gainDB, static_cast<SampleType> (-120),
                        static_cast<SampleType> (120))
                / static_cast<SampleType> (40));
        return normalise ({
            static_cast<SampleType> (1) + alpha * amplitude,
            static_cast<SampleType> (-2) * cosine,
            static_cast<SampleType> (1) - alpha * amplitude,
            static_cast<SampleType> (1) + alpha / amplitude,
            static_cast<SampleType> (-2) * cosine,
            static_cast<SampleType> (1) - alpha / amplitude
        });
    }

    static BiquadCoefficients lowShelf (SampleType sampleRate,
                                        SampleType frequency,
                                        SampleType gainDB) noexcept
    {
        return designShelf (sampleRate, frequency, gainDB, false);
    }

    static BiquadCoefficients highShelf (SampleType sampleRate,
                                         SampleType frequency,
                                         SampleType gainDB) noexcept
    {
        return designShelf (sampleRate, frequency, gainDB, true);
    }

private:
    struct Raw
    {
        SampleType b0, b1, b2, a0, a1, a2;
    };

    bool allFinite() const noexcept
    {
        return std::isfinite (b0) && std::isfinite (b1)
            && std::isfinite (b2) && std::isfinite (a1)
            && std::isfinite (a2);
    }

    static bool prepareFrequency (SampleType sampleRate,
                                  SampleType frequency,
                                  SampleType& sine,
                                  SampleType& cosine) noexcept
    {
        if (! std::isfinite (sampleRate) || sampleRate <= SampleType {}
            || ! std::isfinite (frequency) || frequency <= SampleType {}
            || frequency >= sampleRate * static_cast<SampleType> (0.5))
            return false;

        const auto radians = static_cast<SampleType> (6.28318530717958647692)
                           * frequency / sampleRate;
        sine = std::sin (radians);
        cosine = std::cos (radians);
        return true;
    }

    template <typename CreateRaw>
    static BiquadCoefficients designResonant (
        SampleType sampleRate, SampleType frequency,
        SampleType q, CreateRaw&& createRaw) noexcept
    {
        SampleType sine, cosine;

        if (! prepareFrequency (sampleRate, frequency, sine, cosine)
            || ! std::isfinite (q) || q <= SampleType {})
            return {};

        return normalise (createRaw (
            cosine, sine / (static_cast<SampleType> (2) * q)));
    }

    static BiquadCoefficients designShelf (
        SampleType sampleRate, SampleType frequency,
        SampleType gainDB, bool high) noexcept
    {
        SampleType sine, cosine;

        if (! prepareFrequency (sampleRate, frequency, sine, cosine)
            || ! std::isfinite (gainDB))
            return {};

        const auto amplitude = std::pow (static_cast<SampleType> (10),
            std::clamp (gainDB, static_cast<SampleType> (-120),
                        static_cast<SampleType> (120))
                / static_cast<SampleType> (40));
        const auto alpha = sine * static_cast<SampleType> (0.7071067811865475);
        const auto rootTerm = static_cast<SampleType> (2)
                            * std::sqrt (amplitude) * alpha;
        const auto plus = amplitude + static_cast<SampleType> (1);
        const auto minus = amplitude - static_cast<SampleType> (1);

        if (high)
            return normalise ({
                amplitude * (plus + minus * cosine + rootTerm),
                static_cast<SampleType> (-2) * amplitude
                    * (minus + plus * cosine),
                amplitude * (plus + minus * cosine - rootTerm),
                plus - minus * cosine + rootTerm,
                static_cast<SampleType> (2) * (minus - plus * cosine),
                plus - minus * cosine - rootTerm
            });

        return normalise ({
            amplitude * (plus - minus * cosine + rootTerm),
            static_cast<SampleType> (2) * amplitude
                * (minus - plus * cosine),
            amplitude * (plus - minus * cosine - rootTerm),
            plus + minus * cosine + rootTerm,
            static_cast<SampleType> (-2) * (minus + plus * cosine),
            plus + minus * cosine - rootTerm
        });
    }

    static BiquadCoefficients normalise (const Raw& raw) noexcept
    {
        if (! std::isfinite (raw.a0) || raw.a0 == SampleType {})
            return {};

        const auto inverse = static_cast<SampleType> (1) / raw.a0;
        BiquadCoefficients result {
            raw.b0 * inverse, raw.b1 * inverse, raw.b2 * inverse,
            raw.a1 * inverse, raw.a2 * inverse
        };
        return result.isStable() ? result : BiquadCoefficients {};
    }
};

template <typename SampleType>
struct Biquad
{
    static_assert (std::is_same_v<SampleType, float>
                   || std::is_same_v<SampleType, double>,
                   "Biquad supports float or double");

    void reset() noexcept
    {
        state1 = SampleType {};
        state2 = SampleType {};
    }

    bool setCoefficients (
        BiquadCoefficients<SampleType> nextCoefficients) noexcept
    {
        if (! nextCoefficients.isStable())
        {
            coefficients = {};
            reset();
            return false;
        }

        coefficients = nextCoefficients;
        return true;
    }

    const BiquadCoefficients<SampleType>& getCoefficients() const noexcept
    {
        return coefficients;
    }

    SampleType process (SampleType input) noexcept
    {
        input = std::isfinite (input) ? input : SampleType {};
        const auto output = coefficients.b0 * input + state1;
        state1 = coefficients.b1 * input
               - coefficients.a1 * output + state2;
        state2 = coefficients.b2 * input
               - coefficients.a2 * output;

        constexpr auto threshold = static_cast<SampleType> (1.0e-20);

        if (std::abs (state1) < threshold)
            state1 = SampleType {};

        if (std::abs (state2) < threshold)
            state2 = SampleType {};

        return std::isfinite (output) ? output : SampleType {};
    }

private:
    BiquadCoefficients<SampleType> coefficients;
    SampleType state1 = SampleType {};
    SampleType state2 = SampleType {};
};

} // namespace chardsp
