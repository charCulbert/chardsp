# chardsp

`chardsp` is a small header-only collection of audio DSP primitives. It
contains a finite linear smoother, a Signalsmith elliptic-BLEP saw oscillator,
a five-waveform Nomad oscillator, an ADSR envelope, a lowpass/highpass
state-variable filter, a discrete colored-noise generator, a 4x downsampler,
and release-aware voice-slot management. It uses CHOC buffer views directly in
block-oriented callers.

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
#include "chardsp/chardsp_EllipticSawOscillator.h"
#include "chardsp/chardsp_Oscillator.h"
#include "chardsp/chardsp_ADSREnvelope.h"
#include "chardsp/chardsp_StateVariableFilter.h"
#include "chardsp/chardsp_ColoredNoise.h"
#include "chardsp/chardsp_Downsampler4x.h"
#include "chardsp/chardsp_VoicePool.h"
```

Or add the repository and link its usage requirements:

```cmake
add_subdirectory(path/to/chardsp)
target_link_libraries(my_executable PRIVATE chardsp)
```

`chardsp` is an `INTERFACE` target. It builds no reusable static or dynamic
library.

## Nomad-derived primitives

The following self-contained primitives preserve the active DSP equations from
`daisy-nomad` commit `bab6bd92f13893a5e46fd2df3d38de1991b10962`. They add no
Daisy or Nomad runtime dependency, allocate no memory, and can each be included
independently.

- `ADSREnvelope` provides finite-time exponential and linear attack, decay, and
  release stages. Stage-time, sustain, and shape changes take effect from the
  current level; `retrigger()` is discontinuity-free, while `trigger()` starts
  from zero. `forceRelease(seconds)` is a configurable linear fade. The peak is
  fixed at `1.0`; velocity curves and output scaling belong in the consumer.
- `StateVariableFilter` is the two-integrator-loop TPT topology with selectable
  `FilterMode::lowpass` and `FilterMode::highpass` output. `setCutoffAndQ()`
  applies coefficients immediately, clamps cutoff to `0` through
  `0.48 * sampleRate`, and gives Q a positive floor. Resonance percentages,
  note-domain modulation, smoothing, and periodic coefficient-update policy
  are deliberately outside the filter; compose those with `SmoothedValue` when
  needed.
- `ColoredNoise` selects the exact brown, pink, white, or blue endpoint with
  `NoiseColor`. `reset(seed)` makes the stream repeatable. Every endpoint state
  advances on every `next()` call, so changing color does not change the
  underlying deterministic timeline. Continuous color morphing and
  oscillator-level makeup gain are consumer policy.
- `Downsampler4x` runs the original two-stage half-rate filter with its fixed
  coefficients. `process()` accepts either four scalar oversamples or a
  `std::array<float, 4>`. It owns only fixed filter state and exposes no
  coefficient or oversampling policy.
- `Oscillator` adds Nomad's pitched `saw`, `ramp`, `pulse`, `triangle`, and
  `sine` waveform/shape equations. It renders at 4x through `Downsampler4x` and
  exposes immediate waveform, frequency, shape, and phase-reset control.
  Nomad's hard sync, micro-drift, two-oscillator routing, and physical models
  remain higher-level policy. Noise stays a separate `ColoredNoise` source so
  consumers can give it an explicit level and color.
- `VoicePool<Voice, voiceCount, Key>` owns a fixed array of the consumer's
  `Voice` type and presents the protocol-neutral API `prepare()`, `start()`,
  `stop()`, `setSustain()`, `allKeysOff()`, and `next()`. `Key` is a small
  caller-defined identity: it can be a MIDI note/channel pair, an analog input
  pin plus pitch, or a sequencer token. The pool stores only its voice slots,
  so its memory does not grow with the possible key domain and there is no
  32-key or MIDI ceiling. The caller remains responsible for input scanning,
  debounce, and remembering physical keys after their voices are stolen.
  A released slot remains allocated for exactly as long as its `Voice` reports
  `isActive()`: short releases become free promptly and long releases are never
  cut off by an allocator timeout. Pressure selects a free voice, the oldest
  actually-releasing voice, then the oldest held voice. Re-pressing an active
  release tail applies Nomad's 2 ms forced fade before that key restarts. Each
  instrument defines its own allocation-free `Voice` DSP topology through the
  documented method contract; no base class, virtual call, or runtime
  allocation is introduced.

All six Nomad-derived components are in `namespace chardsp`; their reset, parameter,
allocation, and per-sample methods are `noexcept`.

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

Disable either optional surface with `-DBUILD_TESTING=OFF` or
`-DCHARDSP_BUILD_EXAMPLES=OFF`. Dependency paths never change: missing
submodules make configuration fail with the recursive update command.
