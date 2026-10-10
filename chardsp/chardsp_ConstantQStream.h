// The real-time side of chardsp_ConstantQ.h, whose transform follows CiCueTea
// (https://github.com/jdsierral/CiCueTea, MIT); its notice is in that header.

#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

#include "chardsp_ConstantQ.h"

namespace chardsp
{

enum class ConstantQChannel
{
    mix,
    left,
    right
};

// A ConstantQ over a live stereo signal, as a spectrogram: a column
// of levels in dB per band (-160 at least) every `hop` samples, from the newest
// block, each band read over the hop centred on the block's middle.
//
// ConstantQReading::centre reads only the middle, as most spectrograms do:
// one coefficient per band per hop. A high band's coefficients
// change far faster than that, so a click between two middles goes unseen and
// close tones in one band alias to a slow flicker. peak and mean read every
// coefficient in the hop instead. mean, the default, is the band's power over
// the hop: steady tones read as in centre, noise reads the same in every band
// whatever the hop, and a click reads its energy spread over the hop. peak
// shows a click at its own level, but reads noise higher in the bands with
// more coefficients per hop (the high ones).
//
// For peak and mean, the bands' coefficients over the hop only depend on the
// samples within six of their atom's spreads, so bands are grouped by how many
// samples that is, rounded up to a power of two, and each group analyses only
// those: the same bands, sampled on the coarser bins of a shorter block.
//
// prepare() allocates the ring, holding two of the largest block, while
// nothing pushes. push() runs on the audio thread and only copies both
// channels into the ring. configure() and next() run on another thread (the
// analysis), which sets the transform up and reads the ring; configure()
// allocates, next() does not.
template <typename SampleType>
class ConstantQStream
{
public:
    static_assert (std::is_same_v<SampleType, float>
                   || std::is_same_v<SampleType, double>,
                   "ConstantQStream supports float or double");

    void prepare (int maxBlockSize)
    {
        int capacity = 8;

        while (capacity < 2 * maxBlockSize && capacity < (1 << 30))
            capacity *= 2;

        ringSize = static_cast<uint32_t> (capacity);
        leftRing.reset (new std::atomic<SampleType>[ringSize]());
        rightRing.reset (new std::atomic<SampleType>[ringSize]());
        writeIndex.store (0, std::memory_order_release);
        analysed = 0;
    }

    // False, analysing nothing, when the design isn't valid or its block is
    // over half the ring. Anything changed starts afresh from the newest
    // samples.
    bool configure (const ConstantQDesign& newDesign, int newHop,
                    ConstantQReading newReading = ConstantQReading::mean,
                    ConstantQChannel newChannel = ConstantQChannel::mix)
    {
        newHop = std::max (1, newHop);
        const bool rebuild = newDesign != design || ! transform.isValid();

        if (rebuild)
        {
            design = newDesign;
            transform.prepare (design);
            block.assign (static_cast<size_t> (transform.getBlockSize()),
                          SampleType {});
            magnitudes.assign (static_cast<size_t> (transform.getNumBands()),
                               SampleType {});
        }

        if (rebuild || newHop != hop)
        {
            hop = newHop;
            group();
        }

        reading = newReading;
        channel = newChannel;
        analysed = writeIndex.load (std::memory_order_acquire);
        const auto blockSize = static_cast<uint32_t> (transform.getBlockSize());
        ready = transform.isValid() && blockSize * 2 <= ringSize
             && static_cast<uint32_t> (hop) <= ringSize - blockSize;
        return ready;
    }

    // The bands, with the whole block.
    const ConstantQ<SampleType>& getTransform() const noexcept
    {
        return transform;
    }

    int getHop() const noexcept { return hop; }

    // Audio thread: never blocks or allocates.
    void push (const SampleType* left, const SampleType* right,
               uint32_t frames) noexcept
    {
        if (ringSize == 0)
            return;

        uint32_t index = writeIndex.load (std::memory_order_relaxed);

        for (uint32_t i = 0; i < frames; ++i, ++index)
        {
            leftRing[index % ringSize].store (left[i],
                                              std::memory_order_relaxed);
            rightRing[index % ringSize].store (right[i],
                                               std::memory_order_relaxed);
        }

        writeIndex.store (index, std::memory_order_release);
    }

    // The next column into `decibels` (a value per band), if a hop's worth of
    // samples has arrived since the last, and in `advanced` the samples since
    // the last column. With more than `keep` columns waiting, or any the ring
    // has already overwritten, it skips to the newest few.
    bool next (SampleType* decibels, uint32_t& advanced,
               uint32_t keep = 8) noexcept
    {
        if (! ready)
            return false;

        const auto size = static_cast<uint32_t> (transform.getBlockSize());
        const auto step = static_cast<uint32_t> (hop);
        const uint32_t end = writeIndex.load (std::memory_order_acquire);

        if (end < size)
            return false;

        uint32_t before = analysed;

        if (end - analysed > ringSize - size) // fell behind the ring
            analysed = end - step, before = analysed - step;

        const uint32_t waiting = (end - analysed) / step;

        if (waiting == 0)
            return false;

        if (waiting > keep)
            analysed += (waiting - keep) * step;

        analysed += step;
        advanced = analysed - before;

        for (uint32_t i = 0; i < size; ++i)
        {
            const uint32_t at = (analysed - size + i) % ringSize;
            const auto left = leftRing[at].load (std::memory_order_relaxed);
            const auto right = rightRing[at].load (std::memory_order_relaxed);
            block[i] = channel == ConstantQChannel::left ? left
                : channel == ConstantQChannel::right ? right
                : static_cast<SampleType> (0.5) * (left + right);
        }

        if (reading == ConstantQReading::centre)
        {
            transform.process (block.data());

            for (int b = 0; b < transform.getNumBands(); ++b)
                magnitudes[static_cast<size_t> (b)] =
                    transform.getCentreMagnitude (b);
        }
        else
        {
            const double middle = 0.5 * size;
            bool whole = false;

            for (auto& g : groups)
            {
                if (g.transform.isValid())
                {
                    const auto length = static_cast<uint32_t> (
                        g.transform.getBlockSize());
                    g.transform.process (block.data() + (size - length) / 2);
                    g.transform.getMagnitudes (
                        reading, 0.5 * length - 0.5 * hop,
                        0.5 * length + 0.5 * hop, magnitudes.data() + g.first);
                    continue;
                }

                if (! whole)
                    transform.process (block.data()), whole = true;

                for (int b = g.first; b < g.end; ++b)
                    magnitudes[static_cast<size_t> (b)] = transform.getMagnitude (
                        b, reading, middle - 0.5 * hop, middle + 0.5 * hop);
            }
        }

        for (size_t b = 0; b < magnitudes.size(); ++b)
            decibels[b] = static_cast<SampleType> (std::max (-160.0,
                20.0 * std::log10 (std::max (
                    1.0e-8, static_cast<double> (magnitudes[b])))));

        return true;
    }

private:
    struct Group
    {
        int first = 0, end = 0; // bands
        int length = 0; // samples
        ConstantQ<SampleType> transform; // when not valid, the whole block's
    };

    // Each band's coefficients across the hop need the samples within six of
    // its atom's spreads either side.
    void group()
    {
        groups.clear();
        const int size = transform.getBlockSize();
        int previous = -1;

        for (int b = 0; b < transform.getNumBands(); ++b)
        {
            int length = 4;

            while (length < hop + 12.0 * transform.getSpread (b) && length < size)
                length *= 2;

            if (length != previous)
            {
                groups.emplace_back();
                groups.back().first = b;
                groups.back().length = previous = length;
            }

            groups.back().end = b + 1;
        }

        const auto& frequencies = transform.getFrequencies();

        for (auto& g : groups)
        {
            auto part = design;
            part.minFrequency = frequencies[static_cast<size_t> (g.first)];
            part.maxFrequency = frequencies[static_cast<size_t> (g.end - 1)];
            part.blockSize = g.length;

            // Too long, or bands it can't resolve: the whole block's.
            if (part.blockSize >= size || ! g.transform.prepare (part)
                || g.transform.getNumBands() != g.end - g.first)
                g.transform = ConstantQ<SampleType>();
        }
    }

    ConstantQ<SampleType> transform;
    std::vector<Group> groups;
    ConstantQDesign design;
    int hop = 0;
    ConstantQReading reading = ConstantQReading::mean;
    ConstantQChannel channel = ConstantQChannel::mix;
    bool ready = false;
    std::vector<SampleType> block, magnitudes;
    uint32_t ringSize = 0;
    uint32_t analysed = 0; // the sample the last column ended at
    std::unique_ptr<std::atomic<SampleType>[]> leftRing, rightRing;
    std::atomic<uint32_t> writeIndex { 0 };
};

} // namespace chardsp
