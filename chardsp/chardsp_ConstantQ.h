// The constant-Q transform here follows CiCueTea's NsgfCqtSparse
// (https://github.com/jdsierral/CiCueTea), under its licence:
//
// MIT License
//
// Copyright (c) 2024-2026 Juan Sierra
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "signalsmith-dsp/fft.h"

namespace chardsp
{

// The bands of a constant-Q transform: referenceFrequency · 2^(k / bandsPerOctave)
// for every whole k from minFrequency to maxFrequency, analysed over blocks of
// blockSize samples (a power of two; 0 picks minimumBlockSize()).
struct ConstantQDesign
{
    double sampleRate = 48000.0;
    double bandsPerOctave = 24.0;
    double minFrequency = 55.0;
    double maxFrequency = 8000.0;
    double referenceFrequency = 440.0;
    int blockSize = 0;
};

inline bool operator== (const ConstantQDesign& a,
                        const ConstantQDesign& b) noexcept
{
    return a.sampleRate == b.sampleRate
        && a.bandsPerOctave == b.bandsPerOctave
        && a.minFrequency == b.minFrequency
        && a.maxFrequency == b.maxFrequency
        && a.referenceFrequency == b.referenceFrequency
        && a.blockSize == b.blockSize;
}

inline bool operator!= (const ConstantQDesign& a,
                        const ConstantQDesign& b) noexcept
{
    return ! (a == b);
}

// How a band's level over a stretch of time becomes one value: its magnitude
// at the stretch's middle, the largest magnitude in it, or the root mean
// square of the magnitudes in it.
enum class ConstantQReading
{
    centre,
    peak,
    mean
};

// A constant-Q transform: the sparse non-stationary Gabor CQT of CiCueTea
// (Juan Sierra, MIT), forward only. Each band weighs the block's spectrum by a
// Gaussian in log frequency, e^(-c · (log2 f - log2 fk)²) with
// c = ln 4 · bandsPerOctave², leaving out weights of 1e-6 or less; its
// coefficients are the inverse transform of that, a complex envelope over the
// block, which a full-scale sine on the band's centre holds near 1.
//
// The bands, weights and coefficients are those of CiCueTea's NsgfCqtSparse
// built with minFrequency / 2^(1 / bandsPerOctave) and
// maxFrequency · 2^(1 / bandsPerOctave), less its two edge bands, which are
// low- and high-pass rather than constant-Q.
//
// process() takes one real FFT of a block; everything after reads that
// spectrum. A coefficient at any time is a weighted sum over the band's bins,
// so one value per band (at the block's centre, say) costs
// far less than CiCueTea's inverse FFT per band. Like CiCueTea's, the
// transform treats the block as periodic: a whole file analysed as one block
// wants silence padded at both ends.
//
// prepare() allocates; the rest does not.
template <typename SampleType>
class ConstantQ
{
public:
    static_assert (std::is_same_v<SampleType, float>
                   || std::is_same_v<SampleType, double>,
                   "ConstantQ supports float or double");

    // The default block: the shortest power of two holding bandsPerOctave
    // periods of minFrequency, enough for the lowest band's atom.
    static int minimumBlockSize (double sampleRate, double bandsPerOctave,
                                 double minFrequency) noexcept
    {
        return nextPowerOfTwo (static_cast<int64_t> (
            std::ceil (sampleRate * bandsPerOctave / minFrequency)));
    }

    // False, leaving no bands, for a design without a band, with a band at or
    // above Nyquist, or with a band the block can't resolve (fewer than four
    // bins, CiCueTea's limit).
    bool prepare (const ConstantQDesign& newDesign)
    {
        design = newDesign;
        clear();
        const auto& d = design;

        if (! (d.sampleRate > 0.0) || ! (d.bandsPerOctave > 0.0)
            || ! (d.minFrequency > 0.0)
            || ! (d.minFrequency <= d.maxFrequency)
            || ! (2.0 * d.maxFrequency < d.sampleRate)
            || ! (d.referenceFrequency > 0.0)
            || ! std::isfinite (d.sampleRate + d.bandsPerOctave
                                + d.maxFrequency + d.referenceFrequency))
            return false;

        if (design.blockSize == 0)
            design.blockSize = minimumBlockSize (d.sampleRate,
                                                 d.bandsPerOctave,
                                                 d.minFrequency);

        size = design.blockSize;

        if (size < 4 || size > (1 << 30) || (size & (size - 1)) != 0)
            return clear();

        // CiCueTea's arithmetic, in its order, so these are the same doubles.
        const double fraction = 1.0 / d.bandsPerOctave;
        const double c = std::log (4.0) / (fraction * fraction);
        const double rate = d.sampleRate;
        const double reach = std::sqrt (std::log (1.0 / threshold) / c)
                           + 1.0e-3; // octaves either side above threshold
        const auto lowest = static_cast<int64_t> (std::floor (
            d.bandsPerOctave * std::log2 (d.minFrequency
                                          / d.referenceFrequency))) - 1;
        const auto highest = static_cast<int64_t> (std::ceil (
            d.bandsPerOctave * std::log2 (d.maxFrequency
                                          / d.referenceFrequency))) + 1;

        for (auto k = lowest; k <= highest; ++k)
        {
            const double centre = d.referenceFrequency
                * std::exp (fraction * std::log (2.0) * static_cast<double> (k));

            if (centre < d.minFrequency * (1.0 - 1.0e-9)
                || centre > d.maxFrequency * (1.0 + 1.0e-9))
                continue;

            const int from = std::max (1, static_cast<int> (std::floor (
                centre * std::exp2 (-reach) * size / rate)));
            const int to = std::min (size / 2, static_cast<int> (std::ceil (
                centre * std::exp2 (reach) * size / rate)));
            Band band;
            band.offset = weights.size();

            for (int j = from; j <= to; ++j)
            {
                const double difference =
                    std::log2 (static_cast<double> (j) * rate
                               / static_cast<double> (size))
                    - std::log2 (centre);
                const double weight = std::exp (-c * (difference * difference));

                if (weight <= threshold)
                    continue;

                // The Gaussian falls away from its centre, so the bins above
                // the threshold are one run.
                if (band.count == 0)
                    band.first = j;

                weights.push_back (static_cast<SampleType> (
                    2.0 / size * weight));
                ++band.count;
            }

            if (band.count < 4)
                return clear();

            band.length = nextPowerOfTwo (band.count);
            // Readings over a stretch take coefficients a third of the atom's
            // standard deviation in time apart (or CiCueTea's, if closer): a
            // click's peak then reads within 0.15 dB, wherever it falls. Set
            // by time, not bins, so a shorter block has the same grid.
            // Near its centre the Gaussian is one of standard deviation
            // fk · ln 2 / sqrt (2c) Hz, so its atom spreads 1 / (2π) of the
            // reciprocal in time.
            band.spread = rate / (twoPi * centre * std::log (2.0)
                                  / std::sqrt (2.0 * c));
            band.readingLength = std::max (band.length, std::min (
                size, nextPowerOfTwo (static_cast<int64_t> (
                    std::ceil (3.0 * size / band.spread)))));
            bands.push_back (band);
            frequencies.push_back (centre);
        }

        if (bands.empty())
            return clear();

        int longest = 0;

        for (const auto& band : bands)
            longest = std::max (longest, band.readingLength);

        inverses.clear();

        for (int i = 0; i <= log2Of (longest); ++i) // by log2 of the size
            inverses.emplace_back (size_t { 1 } << std::max (i, 2));

        transform.setSize (static_cast<size_t> (size));
        spectrum.assign (static_cast<size_t> (size / 2 + 1), {});
        scratch.assign (static_cast<size_t> (longest), {});
        envelope.assign (static_cast<size_t> (longest), {});
        return true;
    }

    bool isValid() const noexcept { return ! bands.empty(); }
    const ConstantQDesign& getDesign() const noexcept { return design; }
    int getBlockSize() const noexcept { return size; }
    int getNumBands() const noexcept { return static_cast<int> (bands.size()); }

    // How far a band's atom spreads in time: the standard deviation of its
    // magnitude, in samples. A coefficient depends on the samples within
    // about six of these of its time.
    double getSpread (int bandIndex) const noexcept
    {
        return bands[static_cast<size_t> (bandIndex)].spread;
    }

    // Each band's centre in Hz, low to high.
    const std::vector<double>& getFrequencies() const noexcept
    {
        return frequencies;
    }

    // Takes in a block of getBlockSize() samples.
    void process (const SampleType* block) noexcept
    {
        if (! isValid())
            return;

        transform.fft (block, spectrum);
        // The FFT packs Nyquist into DC's imaginary part.
        spectrum[static_cast<size_t> (size / 2)] = spectrum[0].imag();
        spectrum[0].imag (SampleType {});
    }

    // The band's coefficient `time` samples into the block (any real time;
    // the block repeats).
    std::complex<SampleType> getCoefficient (int bandIndex,
                                             double time) const noexcept
    {
        const auto& band = bands[static_cast<size_t> (bandIndex)];
        const double turns = std::fmod (time, static_cast<double> (size))
                           / size;
        const auto start = std::polar (1.0, twoPi * std::fmod (
            band.first * turns, 1.0));
        const auto step = std::polar (1.0, twoPi * turns);
        return static_cast<std::complex<SampleType>> (
            sum (band, start, step));
    }

    // The band's magnitude at the block's centre.
    SampleType getCentreMagnitude (int bandIndex) const noexcept
    {
        // At the centre, bin j turns by (-1)^j.
        const auto& band = bands[static_cast<size_t> (bandIndex)];
        const SampleType* w = weights.data() + band.offset;
        const std::complex<SampleType>* x = spectrum.data() + band.first;
        double evenReal = 0.0, evenImag = 0.0, oddReal = 0.0, oddImag = 0.0;
        int m = 0;

        for (; m + 1 < band.count; m += 2)
        {
            evenReal += static_cast<double> (w[m]) * x[m].real();
            evenImag += static_cast<double> (w[m]) * x[m].imag();
            oddReal += static_cast<double> (w[m + 1]) * x[m + 1].real();
            oddImag += static_cast<double> (w[m + 1]) * x[m + 1].imag();
        }

        if (m < band.count)
        {
            evenReal += static_cast<double> (w[m]) * x[m].real();
            evenImag += static_cast<double> (w[m]) * x[m].imag();
        }

        return static_cast<SampleType> (std::hypot (evenReal - oddReal,
                                                    evenImag - oddImag));
    }

    // CiCueTea's coefficients for a band: getNumCoefficients() of them, the
    // nth at n · getBlockSize() / getNumCoefficients() samples into the block.
    int getNumCoefficients (int bandIndex) const noexcept
    {
        return bands[static_cast<size_t> (bandIndex)].length;
    }

    void getCoefficients (int bandIndex,
                          std::complex<SampleType>* coefficients) noexcept
    {
        const auto& band = bands[static_cast<size_t> (bandIndex)];
        inverse (band, band.length);

        for (int n = 0; n < band.length; ++n)
        {
            // CiCueTea's phase: time measured from the block's start.
            const auto turn = static_cast<double> (
                (static_cast<int64_t> (band.first) * n) % band.length)
                / band.length;
            coefficients[n] = envelope[static_cast<size_t> (n)]
                * static_cast<std::complex<SampleType>> (
                    std::polar (1.0, twoPi * turn));
        }
    }

    // A band's level from `from` to `to` samples into the block, read from
    // its coefficients there (at least the one nearest the middle).
    SampleType getMagnitude (int bandIndex, ConstantQReading reading,
                             double from, double to) noexcept
    {
        const double middle = 0.5 * (from + to);

        if (reading == ConstantQReading::centre)
            return middle == 0.5 * size
                ? getCentreMagnitude (bandIndex)
                : std::abs (getCoefficient (bandIndex, middle));

        const auto& band = bands[static_cast<size_t> (bandIndex)];
        const int length = band.readingLength;
        const double spacing = static_cast<double> (size) / length;
        auto first = static_cast<int> (std::ceil (
            std::max (from, 0.0) / spacing));
        auto last = static_cast<int> (std::ceil (
            std::min (to, static_cast<double> (size)) / spacing)) - 1;

        if (first > last)
            first = last = static_cast<int> (std::lround (middle / spacing))
                         % length;

        const int count = last - first + 1;
        double peak = 0.0, power = 0.0;

        // Each coefficient alone costs a sum over the band's bins; all of
        // them, an inverse FFT. Whichever is less work.
        if (static_cast<int64_t> (count) * band.count
            <= static_cast<int64_t> (length) * log2Of (length))
        {
            const auto step = std::polar (1.0, twoPi / length);
            auto rotation = std::polar (1.0, twoPi * first / length);

            for (int n = first; n <= last; ++n, rotation *= step)
            {
                const auto start = std::polar (1.0, twoPi * static_cast<double> (
                    (static_cast<int64_t> (band.first) * n) % length)
                    / length);
                const double magnitude = std::abs (sum (band, start, rotation));
                peak = std::max (peak, magnitude);
                power += magnitude * magnitude;
            }
        }
        else
        {
            inverse (band, length);

            for (int n = first; n <= last; ++n)
            {
                const double magnitude = std::abs (
                    std::complex<double> (envelope[static_cast<size_t> (n)]));
                peak = std::max (peak, magnitude);
                power += magnitude * magnitude;
            }
        }

        return static_cast<SampleType> (reading == ConstantQReading::peak
            ? peak : std::sqrt (power / count));
    }

    // getMagnitude() for every band, into getNumBands() values.
    void getMagnitudes (ConstantQReading reading, double from, double to,
                        SampleType* magnitudes) noexcept
    {
        for (int b = 0; b < getNumBands(); ++b)
            magnitudes[b] = getMagnitude (b, reading, from, to);
    }

    // One band over a run of columns `spacing` samples apart, the first
    // centred `firstTime` samples into the block: one inverse FFT, then each
    // column read from the coefficients within half a spacing of its time
    // (centre: the one nearest). For analysing a long block at any time
    // resolution.
    void getColumns (int bandIndex, ConstantQReading reading,
                     double firstTime, double spacing, int numColumns,
                     SampleType* magnitudes) noexcept
    {
        const auto& band = bands[static_cast<size_t> (bandIndex)];
        const int length = band.readingLength;
        inverse (band, length);
        const double step = static_cast<double> (size) / length;
        const auto at = [&] (int64_t n) {
            n %= length;
            return std::abs (std::complex<double> (envelope[static_cast<size_t> (
                n < 0 ? n + length : n)]));
        };

        for (int c = 0; c < numColumns; ++c)
        {
            const double time = firstTime + c * spacing;
            auto first = static_cast<int64_t> (std::ceil ((time - 0.5 * spacing)
                                                          / step));
            auto last = static_cast<int64_t> (std::ceil ((time + 0.5 * spacing)
                                                         / step)) - 1;

            if (reading == ConstantQReading::centre || first > last)
                first = last = std::llround (time / step);

            double peak = 0.0, power = 0.0;

            for (auto n = first; n <= last; ++n)
            {
                const double magnitude = at (n);
                peak = std::max (peak, magnitude);
                power += magnitude * magnitude;
            }

            magnitudes[c] = static_cast<SampleType> (
                reading == ConstantQReading::mean
                    ? std::sqrt (power / static_cast<double> (last - first + 1))
                    : peak);
        }
    }

private:
    static constexpr double threshold = 1.0e-6; // CiCueTea's
    static constexpr double twoPi = 6.283185307179586476925286766559;

    struct Band
    {
        int first = 0;  // bin
        int count = 0;  // bins
        int length = 0; // coefficients: count rounded up to a power of two
        int readingLength = 0; // coefficients for readings
        double spread = 0.0; // samples
        size_t offset = 0; // into weights
    };

    static int nextPowerOfTwo (int64_t value) noexcept
    {
        int64_t power = 4;

        while (power < value && power < (int64_t { 1 } << 30))
            power *= 2;

        return static_cast<int> (power);
    }

    static int log2Of (int power) noexcept
    {
        int bits = 0;

        while ((1 << bits) < power)
            ++bits;

        return bits;
    }

    bool clear()
    {
        bands.clear();
        frequencies.clear();
        weights.clear();
        size = 0;
        return false;
    }

    // Σ w_m · X_(first + m) · start · step^m, in double.
    std::complex<double> sum (const Band& band, std::complex<double> start,
                              std::complex<double> step) const noexcept
    {
        // Spelled out: std::complex's multiply checks for infinities.
        const SampleType* w = weights.data() + band.offset;
        const std::complex<SampleType>* x = spectrum.data() + band.first;
        const double stepReal = step.real(), stepImag = step.imag();
        double turnReal = start.real(), turnImag = start.imag();
        double totalReal = 0.0, totalImag = 0.0;

        for (int m = 0; m < band.count; ++m)
        {
            const double real = w[m] * static_cast<double> (x[m].real());
            const double imag = w[m] * static_cast<double> (x[m].imag());
            totalReal += real * turnReal - imag * turnImag;
            totalImag += real * turnImag + imag * turnReal;
            const double nextReal = turnReal * stepReal - turnImag * stepImag;
            turnImag = turnReal * stepImag + turnImag * stepReal;
            turnReal = nextReal;
        }

        return { totalReal, totalImag };
    }

    // The band's envelope, without CiCueTea's phase, into `envelope`.
    void inverse (const Band& band, int length) noexcept
    {
        const SampleType* w = weights.data() + band.offset;
        const std::complex<SampleType>* x = spectrum.data() + band.first;
        std::fill (scratch.begin(), scratch.begin() + length,
                   std::complex<SampleType> {});

        for (int m = 0; m < band.count; ++m)
            scratch[static_cast<size_t> (m)] = w[m] * x[m];

        inverses[static_cast<size_t> (log2Of (length))].ifft (
            scratch.data(), envelope.data());
    }

    ConstantQDesign design;
    int size = 0;
    std::vector<Band> bands;
    std::vector<double> frequencies;
    std::vector<SampleType> weights;
    std::vector<std::complex<SampleType>> spectrum, scratch, envelope;
    signalsmith::fft::RealFFT<SampleType> transform;
    std::vector<signalsmith::fft::FFT<SampleType>> inverses;
};

} // namespace chardsp
