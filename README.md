# chardsp

`chardsp` is a small header-only collection of audio DSP primitives. It
contains a finite linear smoother, naive/PolyBLEP/elliptic oscillator families,
a separate five-waveform Nomad oscillator, an ADSR envelope, a
lowpass/bandpass/highpass state-variable filter, a discrete colored-noise
generator, a 4x downsampler, release-aware voice-slot management, and
protocol-neutral mono/legato/portamento policy. It uses CHOC buffer views
directly in block-oriented callers.

Runtime events, parameter schemas and state, MIDI mapping, scheduling,
diagnostics transport, device hosting, UI, WebViews and command-line handling
do not belong in this repository.

## Dependencies

CHOC and Signalsmith `elliptic-blep` are pinned Git submodules:

```text
external/choc
external/elliptic-blep
```

Clone recursively:

```sh
git clone --recurse-submodules https://github.com/charCulbert/chardsp.git
```

For an existing checkout:

```sh
git submodule update --init --recursive
```

CHOC is ISC-licensed and Signalsmith `elliptic-blep` is MIT-licensed. Their
license notices remain in their respective submodules. The source project did
not establish a project license, so this extraction does not invent one.

## Use

Include headers directly:

```cpp
#include "chardsp/chardsp_SmoothedValue.h"
#include "chardsp/chardsp_Oscillator.h"
#include "chardsp/chardsp_NomadOscillators.h"
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
library.

All sample-processing types are direct `float`/`double` templates. Select the
precision at the use site, for example:

```cpp
chardsp::PolyBLEPSawOscillator<float> realtimeOscillator;
chardsp::StateVariableFilter<double> analysisFilter;
```

There are no float-default or `Double` compatibility aliases.

## Nomad-derived primitives

The following self-contained primitives preserve the active DSP equations from
`daisy-nomad` commit `bab6bd92f13893a5e46fd2df3d38de1991b10962`. They add no
Daisy or Nomad runtime dependency, allocate no memory, and can each be included
independently.

- `ADSREnvelope<SampleType>` provides finite-time exponential and linear attack, decay, and
  release stages. Stage-time, sustain, and shape changes take effect from the
  current level; `retrigger()` is discontinuity-free, while `trigger()` starts
  from zero. `forceRelease(seconds)` is a configurable linear fade. The peak is
  fixed at `1.0`; velocity curves and output scaling belong in the consumer.
- `StateVariableFilter<SampleType>` is the two-integrator-loop TPT topology
  with selectable `FilterMode::lowpass`, `FilterMode::bandpass`, and
  `FilterMode::highpass` output. `setCutoffAndQ()`
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
- `NomadOscillator<SampleType>` in `chardsp_NomadOscillators.h` preserves
  Nomad's combined `saw`, `ramp`, `pulse`, shaped/folded `triangle`, and
  shaped/folded `sine` interface. Its shape semantics and internal 4x path are
  intentionally isolated here rather than being properties of a generic
  oscillator. Noise remains a separate `ColoredNoise` source. Hard sync,
  micro-drift, two-oscillator routing, and physical models remain higher-level
  policy.
- `VoicePool<Voice, voiceCount, Key>` owns a fixed array of the consumer's
  `Voice` type and presents the protocol-neutral API `prepare()`, `start()`,
  `stop()`, `setSustain()`, `allKeysOff()`, and `next()`. `Key` is a small
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
- `BasicSynthVoice<SampleType>` composes two `NomadOscillator` instances and
  colored noise into an ADSR and LP/BP/HP filter. It accepts fractional
  semitone pitch and velocity directly. Velocity scales the normalized ADSR
  level in the voice, following the same separation used by JUCE's ADSR and
  synthesiser voices. `setVelocitySensitivity()` blends from fixed level to
  full velocity response; `setVelocityCurve()` selects a power exponent where
  `1` is linear, values below `1` lift softer notes, and values above `1`
  increase dynamic contrast. The voice exposes its other component parameters
  and implements the `release()`, forced-fade, availability, and `next()` side
  of the `VoicePool` contract. A tiny consumer wrapper maps its own input-key
  type to pitch, keeping MIDI and hardware scanning out of the DSP class.
- `MonoVoiceController<SampleType, Key, heldCapacity>` implements Nomad's
  last-note-priority monophonic policy without owning a DSP voice or knowing
  about MIDI. It returns simple pitch/release actions for the consumer to
  apply. Legato suppresses envelope retriggers while the gate is active and
  uses the Cmajor implementation's 24 ms grace window for closely spaced
  notes. Sustain, fallback to the newest still-held key, and off/always/
  fingered `PortamentoMode` choices are included. The held-key capacity is an
  explicit compile-time choice instead of a hidden 16- or 32-note limit.
- `Portamento<SampleType>` interpolates in semitone pitch space with Nomad's
  exact linear-to-analog shape curve. `setTimeRateMorph(0)` gives constant
  time, `setTimeRateMorph(1)` gives constant time per octave, and intermediate
  values continuously blend those policies. Pitch-to-frequency conversion and
  pitch bend remain consumer concerns, so bend can be applied after glide.

All components are in `namespace chardsp`; their reset, parameter, allocation,
and per-sample methods are `noexcept`.

## Build and test

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DCHARDSP_BUILD_EXAMPLES=ON
cmake --build build
ctest --test-dir build --output-on-failure
./build/examples/bufferRender/chardsp_buffer_render chardsp_render.wav
```

`bufferRender` remains the focused two-second elliptic-BLEP saw example. It
applies explicit fade-in and fade-out smoothing, and uses CHOC's WAV writer to
create a 48 kHz, 16-bit stereo file. The output path is optional and defaults
to `chardsp_render.wav`.

The test executable uses CHOC's unit-test framework and includes Nomad parity
vectors, float/double instantiation, filter-response checks, allocation checks,
and sample-timed voice-stealing burst cases.

Disable either optional surface with `-DBUILD_TESTING=OFF` or
`-DCHARDSP_BUILD_EXAMPLES=OFF`. Dependency paths never change: missing
submodules make configuration fail with the recursive update command.
