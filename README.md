# chardsp

`chardsp` is a small header-only collection of audio DSP primitives. It
contains a finite linear smoother, a Signalsmith elliptic-BLEP saw oscillator,
an ADSR envelope, a lowpass/highpass state-variable filter, a discrete
colored-noise generator, and a 4x downsampler. It uses CHOC buffer views
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
#include "chardsp/chardsp_EllipticSawOscillator.h"
#include "chardsp/chardsp_ADSREnvelope.h"
#include "chardsp/chardsp_StateVariableFilter.h"
#include "chardsp/chardsp_ColoredNoise.h"
#include "chardsp/chardsp_Downsampler4x.h"
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

All four types are in `namespace chardsp`; their preparation, reset, parameter,
and per-sample methods are `noexcept`.

## Build and test

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DCHARDSP_BUILD_EXAMPLES=ON
cmake --build build
ctest --test-dir build --output-on-failure
./build/examples/bufferRender/chardsp_buffer_render chardsp_render.wav
```

The example renders two seconds of elliptic-BLEP saw DSP into a CHOC buffer,
applies explicit fade-in and fade-out smoothing, and uses CHOC's WAV writer to
create a 48 kHz, 16-bit stereo file. The output path is optional and defaults
to `chardsp_render.wav`.

Disable either optional surface with `-DBUILD_TESTING=OFF` or
`-DCHARDSP_BUILD_EXAMPLES=OFF`. Dependency paths never change: missing
submodules make configuration fail with the recursive update command.
