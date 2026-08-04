# chardsp

`chardsp` is a small header-only collection of audio DSP primitives. It
contains fixed delay, gain/crossfade and smoothing utilities, DC/SVF/biquad
filters, peak/RMS/envelope detectors, caller-owned sample playback,
naive/PolyBLEP/elliptic oscillator families, an ADSR envelope, colored noise,
a specialized 4x downsampler,
complete 2x/4x oversampling, release-aware voice-slot management, and
protocol-neutral mono/legato/portamento policy. It uses CHOC views directly in
block-oriented callers.

Runtime events, parameter schemas and state, MIDI mapping, scheduling,
diagnostics transport, device hosting, UI, WebViews and command-line handling
do not belong in this repository.

## Dependencies

CHOC, Signalsmith DSP, and Signalsmith `elliptic-blep` are pinned Git
submodules:

```text
external/choc
external/elliptic-blep
external/signalsmith-dsp
```

Signalsmith DSP is pinned to revision
`2d20161915e733f117545c6be8cd3275a739a1e3` (`v1.7.1`).

Clone recursively:

```sh
git clone --recurse-submodules https://github.com/charCulbert/chardsp.git
```

For an existing checkout:

```sh
git submodule update --init --recursive
```

CHOC is ISC-licensed. Signalsmith DSP and Signalsmith `elliptic-blep` are
MIT-licensed. Their license notices remain in their respective submodules.
chardsp is ISC-licensed; see [LICENSE](LICENSE).

Runtime notice collection and source distribution are separate. Applications
receive the chardsp and CHOC notices through every public chardsp header, plus
only the implementation dependencies actually included: oscillator headers
register `elliptic-blep`, and the oversampler header registers Signalsmith
DSP. `chardsp_OpenSourceLicenses.h` ensures CHOC's collector is included before
other CHOC headers, and CHOC de-duplicates registrations across translation
units. The original `LICENSE` files remain mandatory in recursive
source/submodule distributions whether or not a component is instantiated.

## Use

Include headers directly:

```cpp
#include "chardsp/chardsp_SmoothedValue.h"
#include "chardsp/chardsp_DelayLine.h"
#include "chardsp/chardsp_Gain.h"
#include "chardsp/chardsp_DCBlocker.h"
#include "chardsp/chardsp_Biquad.h"
#include "chardsp/chardsp_Detectors.h"
#include "chardsp/chardsp_SamplePlayer.h"
#include "chardsp/chardsp_Oversampler.h"
#include "chardsp/chardsp_Oscillator.h"
#include "chardsp/chardsp_ADSREnvelope.h"
#include "chardsp/chardsp_BasicSynthVoice.h"
#include "chardsp/chardsp_StateVariableFilter.h"
#include "chardsp/chardsp_ColoredNoise.h"
#include "chardsp/chardsp_Downsampler4x.h"
#include "chardsp/chardsp_VoicePool.h"
#include "chardsp/chardsp_MonoVoiceController.h"
#include "chardsp/chardsp_Portamento.h"
```

Or add the repository and link its usage requirements:

```cmake
add_subdirectory(path/to/chardsp)
target_link_libraries(my_executable PRIVATE chardsp)
```

`chardsp` is an `INTERFACE` target. It builds no reusable static or dynamic
library. In-tree CMake consumers may also use the `chardsp::chardsp` alias.

Signalsmith DSP is an internal implementation dependency. Public chardsp
signatures contain only chardsp, standard C++, or CHOC types.

All sample-processing types are direct `float`/`double` templates. Select the
precision at the use site, for example:

```cpp
chardsp::PolyBLEPSawOscillator<float> realtimeOscillator;
chardsp::StateVariableFilter<double> analysisFilter;
```

There are no float-default or `Double` compatibility aliases.

## Foundational primitives

- `DelayLine<SampleType, capacity>` owns a fixed `std::array`. A zero delay
  reads the most recently written value and `maximumDelay` reads the oldest
  retained value. Integer and linear-interpolated reads are clamped, and
  separate `read()`/`write()` operations support feedback topologies.
- `gain`, `linearCrossfade`, and `equalPowerCrossfade` are stateless scalar
  helpers. CHOC already supplies block gain/copy/add and generic sample
  traversal, so chardsp does not duplicate those buffer utilities.
- `SmoothedValue` provides exact finite linear and multiplicative ramps,
  `getTarget()`, `isSmoothing()`, and closed-form `skip()`. Multiplicative
  requests involving zero or a sign change deliberately use a linear ramp.
- `DCBlocker` is a first-order filter with a documented pole cutoff and an
  explicit zero-cutoff bypass.
- `StateVariableFilter::processAll()` advances the TPT state once and returns
  lowpass, bandpass, highpass, and notch simultaneously. `process()` retains
  the selected-mode fast interface.
- `BiquadCoefficients` separates normalized RBJ lowpass, highpass, bandpass,
  notch, peak, low-shelf, and high-shelf design from the transposed-direct-form
  `Biquad` state. Invalid or unstable coefficient requests become bypass;
  smoothing/interpolation remains caller policy.
- `PeakFollower`, `RMSFollower`, and `EnvelopeFollower` use explicit 1/e
  release, mean-square, and attack/release time constants. They own no dynamic
  storage and feed charruntime's bounded meter snapshots.
- `SamplePlayer` reads a caller-owned `choc::span<const SampleType>`, supports
  fractional forward rate/position with linear interpolation, and returns zero
  after the span ends. It owns no files, decoding, streaming, or sample memory.
- `Oversampler` wraps pinned Signalsmith FIR stages behind CHOC views. It
  supports prepared multichannel 2x/4x upsampling, in-place high-rate access,
  downsampling, reset, limit checks, and round-trip latency reporting. All
  scratch allocation occurs in `prepare()`. Signalsmith types remain private
  implementation details.

## Composed primitives

These self-contained primitives allocate no memory during rendering and can
each be included independently.

- `ADSREnvelope<SampleType>` provides finite-time exponential and linear attack, decay, and
  release stages. Stage-time, sustain, and shape changes take effect from the
  current level; `retrigger()` is discontinuity-free, while `trigger()` starts
  from zero. The peak is fixed at `1.0`; velocity curves and output scaling
  belong in the consumer.
- `StateVariableFilter<SampleType>` is the two-integrator-loop TPT topology
  with selectable `FilterMode::lowpass`, `FilterMode::bandpass`, and
  `FilterMode::highpass` output. `processAll()` also returns lowpass, bandpass,
  highpass, and notch from one state update. `setCutoffAndQ()`
  applies coefficients immediately, clamps cutoff to `0` through
  `0.48 * sampleRate`, and gives Q a positive floor. Resonance percentages,
  note-domain modulation, smoothing, and periodic coefficient-update policy
  are deliberately outside the filter; compose those with `SmoothedValue` when
  needed. Zero-input processing snaps inaudibly tiny integrator state to exact
  zero, providing portable denormal protection and an exact-zero fast path.
- `ColoredNoise<SampleType>` selects the exact brown, pink, white, or blue endpoint with
  `NoiseColor`. `reset(seed)` makes the stream repeatable. Every endpoint state
  advances on every `next()` call, so changing color does not change the
  underlying deterministic timeline. Continuous color morphing and
  oscillator-level makeup gain are consumer policy.
- `Downsampler4x<SampleType>` runs the original two-stage half-rate filter with its fixed
  coefficients. `process()` accepts either four scalar oversamples or a
  `std::array<SampleType, 4>`. It owns only fixed filter state and exposes no
  coefficient or oversampling policy.
- `chardsp_Oscillator.h` contains explicit algorithm choices rather than one
  oscillator with hidden quality policy. `NaiveSawOscillator`,
  `NaivePulseOscillator`, `NaiveTriangleOscillator`, and `SineOscillator`
  render their equations directly. `PolyBLEPSawOscillator`,
  `PolyBLEPPulseOscillator`, and `PolyBLEPTriangleOscillator` correct value or
  slope discontinuities with polynomial transitions. `EllipticSawOscillator`,
  `EllipticPulseOscillator`, and `EllipticTriangleOscillator` use Signalsmith's
  order-1 BLEP for value jumps and order-2 BLAMP for triangle corners. Saw
  classes expose `SawDirection::descending` and `SawDirection::ascending`;
  pulse classes expose pulse width. Every class has the same `prepare()`,
  `reset()`, `setFrequency()`, `getFrequency()`, `advance()`, and `next()`
  lifecycle. None oversamples internally.
- `VoicePool<Voice, voiceCount, Key>` owns a fixed array of the consumer's
  `Voice` type and presents the protocol-neutral API `prepare()`, `start()`,
  `stop()`, `setSustain()`, `releaseAll()`, `reset()`, and `next()`. `Key` is a small
  caller-defined identity: it can be a MIDI note/channel pair, an analog input
  pin plus pitch, or a sequencer token. The pool stores only its voice slots,
  so its memory does not grow with the possible key domain and there is no
  32-key or MIDI ceiling. The caller remains responsible for input scanning,
  debounce, and remembering physical keys after their voices are stolen.
  A released slot remains allocated until its `Voice::isAvailable()` returns
  true: short releases become free promptly and long releases are never
  cut off by an allocator timeout. Pressure selects a free voice, the oldest
  actually-releasing voice, then the oldest held voice. Every active voice
  selected for stealing finishes a configurable forced fade before its pending
  replacement starts; the default is 2 ms and `setStealFadeTime(0)` selects
  immediate replacement. Pending state is fixed per voice slot. If a burst is
  larger than the voice count while all slots are fading, newer events replace
  the oldest pending starts, so the most recent keys win without an unbounded
  event queue. A pending key's `stop()` cancels that future start. Each
  instrument defines its own allocation-free `Voice` DSP topology through the
  documented method contract. Every fixed voice receives `next()` on every
  frame and owns its available-state fast path; it can return zero immediately
  or advance a free-running source first. No base class, virtual call, or
  runtime allocation is introduced.
- `BasicSynthVoice<SampleType>` is a deliberately small fixed topology built
  from public components: two independently selectable PolyBLEP saw, pulse,
  triangle, or sine oscillators, optional coloured noise, ADSR, and a
  lowpass/bandpass/highpass state-variable filter. It accepts fractional
  semitone pitch and velocity, exposes mixer, pulse-width, envelope, filter,
  noise-colour, and velocity-response controls, and satisfies `VoicePool`'s
  voice contract. Drift and product-specific oscillator policy remain
  consumer concerns.
- `MonoVoiceController<SampleType, Key, heldCapacity>` implements a
  last-note-priority monophonic policy without owning a DSP voice or knowing
  about MIDI. It returns simple pitch/release actions for the consumer to
  apply. Legato suppresses envelope retriggers while the gate is active and
  uses the Cmajor implementation's 24 ms grace window for closely spaced
  notes. Sustain, fallback to the newest still-held key, and off/always/
  fingered `PortamentoMode` choices are included. The held-key capacity is an
  explicit compile-time choice instead of a hidden 16- or 32-note limit.
- `Portamento<SampleType>` interpolates in semitone pitch space with a
  linear-to-exponential shape curve. `setTimeRateMorph(0)` gives constant
  time, `setTimeRateMorph(1)` gives constant rate, and intermediate
  values continuously blend those policies. Pitch-to-frequency conversion and
  pitch bend remain consumer concerns, so bend can be applied after glide.

All components are in `namespace chardsp`; their reset, parameter, allocation,
and per-sample methods are `noexcept`.

## Build and test

```sh
cmake -S . -B build -DCHARDSP_BUILD_TESTS=ON -DCHARDSP_BUILD_EXAMPLES=ON
cmake --build build
./build/tests/chardsp_tests
./build/examples/chardsp_adsr
```

See [the example catalog](examples/README.md) for interactive programs that
demonstrate each oscillator family, ADSR behavior, every filter and noise mode,
fixed feedback delay, complete oversampling, explicit specialized 4x
downsampling, voice stealing, mono portamento, and a polyphonic basic synth.
The examples share only their CHOC audio, MIDI, window, and WebView host.

The test executable uses CHOC's unit-test framework and includes reference
vectors, float/double instantiation, filter/detector response checks, delay and
playback boundaries, multi-translation-unit linkage, allocation checks,
oversampling latency/passband/stopband/nonlinear alias measurements, and
sample-timed voice-stealing burst cases.

Disable either optional surface with `-DCHARDSP_BUILD_TESTS=OFF` or
`-DCHARDSP_BUILD_EXAMPLES=OFF`. Dependency paths never change: missing
submodules make configuration fail with the recursive update command.
