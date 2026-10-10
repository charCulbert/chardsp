# chardsp

Small C++17 header-only audio DSP library. Components are independent,
allocation-free during sample processing, and support `float` or `double`.

## Components

- [`chardsp_ADSREnvelope.h`](chardsp/chardsp_ADSREnvelope.h) — finite-time
  ADSR with linear or exponential stages, live edits, continuous retriggering,
  a forced linear fade for voice stealing, and stage reporting.
- [`chardsp_AllpassDiffuser.h`](chardsp/chardsp_AllpassDiffuser.h) — series
  Schroeder allpass stages with millisecond stage delays and a shared gain.
- [`chardsp_AnalysisBuffer.h`](chardsp/chardsp_AnalysisBuffer.h) — a
  lock-free multichannel ring from the audio thread to an analysis thread,
  with a hop clock that skips ahead when the reader falls behind and reads of
  the newest samples of any channel up to each hop.
- [`chardsp_BasicSynthVoice.h`](chardsp/chardsp_BasicSynthVoice.h) —
  combines two selectable oscillators, pulse width, colored noise, ADSR, a
  lowpass/bandpass/highpass filter, and velocity response into a synth voice.
- [`chardsp_Biquad.h`](chardsp/chardsp_Biquad.h) — stable biquad processing
  with lowpass, highpass, bandpass, notch, peak, and shelf designs.
- [`chardsp_ColoredNoise.h`](chardsp/chardsp_ColoredNoise.h) — deterministic
  seeded brown, pink, white, and blue noise.
- [`chardsp_ConstantQ.h`](chardsp/chardsp_ConstantQ.h) — constant-Q
  transform after CiCueTea's sparse NSGF-CQT: any bands per octave and
  tuning, complex coefficients per band at any time in a block of any power
  of two, and centre, peak, or mean readings over a stretch.
- [`chardsp_ConstantQStream.h`](chardsp/chardsp_ConstantQStream.h) — a
  constant-Q spectrogram of a live stereo signal: a column of dB per hop,
  with the high bands read from shorter blocks.
- [`chardsp_DCBlocker.h`](chardsp/chardsp_DCBlocker.h) — first-order DC
  removal with adjustable cutoff and bypass.
- [`chardsp_DelayLine.h`](chardsp/chardsp_DelayLine.h) — fixed-capacity
  integer or linearly interpolated fractional delay with separate reads and
  writes for feedback.
- [`chardsp_Detectors.h`](chardsp/chardsp_Detectors.h) — peak, RMS, and
  attack/release envelope followers.
- [`chardsp_EllipticBLEP.h`](chardsp/chardsp_EllipticBLEP.h) — Signalsmith
  correction state for fractional step and slope discontinuities.
- [`chardsp_FractionalDelayLine.h`](chardsp/chardsp_FractionalDelayLine.h) —
  runtime-capacity Catmull-Rom fractional delay with separate reads and writes
  for feedback.
- [`chardsp_Gain.h`](chardsp/chardsp_Gain.h) — linear and equal-power
  crossfades.
- [`chardsp_MonoVoiceController.h`](chardsp/chardsp_MonoVoiceController.h) —
  last-note-priority note logic with legato grace, sustain, note fallback, and
  off, always, or fingered portamento actions.
- [`chardsp_OnePole.h`](chardsp/chardsp_OnePole.h) — first-order lowpass or
  highpass with an adjustable cutoff and a shared settled state.
- [`chardsp_Oscillator.h`](chardsp/chardsp_Oscillator.h) — naive, PolyBLEP,
  and Signalsmith elliptic-BLEP saw, pulse, and triangle oscillators, plus
  sine, saw direction, and pulse width.
- [`chardsp_OscillatorEvents.h`](chardsp/chardsp_OscillatorEvents.h) —
  allocation-free fractional edge scheduling for moving pulse waves.
- [`chardsp_OscillatorPhase.h`](chardsp/chardsp_OscillatorPhase.h) —
  normalized phase accumulation with arbitrary advances and fractional wrap
  timing.
- [`chardsp_Oversampler.h`](chardsp/chardsp_Oversampler.h) — prepared
  multichannel Signalsmith FIR 2x/4x block upsampling and downsampling with
  CHOC views and latency reporting.

- [`chardsp_Portamento.h`](chardsp/chardsp_Portamento.h) — semitone-space
  pitch glides with variable curve and constant-time-to-constant-rate morphing.
- [`chardsp_SamplePlayer.h`](chardsp/chardsp_SamplePlayer.h) — forward mono
  playback from caller-owned samples with rate, position, and linear
  interpolation.
- [`chardsp_SimpleDownsampler.h`](chardsp/chardsp_SimpleDownsampler.h) —
  allocation-free power-of-two decimation for externally rendered samples.
- [`chardsp_SmoothedValue.h`](chardsp/chardsp_SmoothedValue.h) — finite
  linear or multiplicative ramps with retargeting and fast skipping.
- [`chardsp_StateVariableFilter.h`](chardsp/chardsp_StateVariableFilter.h) —
  TPT state-variable filter with cutoff, Q, selectable lowpass/bandpass/highpass,
  and simultaneous lowpass, bandpass, highpass, and notch outputs.
- [`chardsp_VoicePool.h`](chardsp/chardsp_VoicePool.h) — fixed polyphony with
  custom key types, sustain, normal release, release-aware stealing, and a
  configurable forced-steal fade.

See [examples](examples/README.md) for interactive component demos and the
polyphonic basic synth.

## Dependencies

Pinned Git submodules:

- [CHOC](https://github.com/Tracktion/choc) — ISC
- [Signalsmith DSP](https://github.com/Signalsmith-Audio/dsp) — MIT
- [Signalsmith elliptic-blep](https://github.com/Signalsmith-Audio/elliptic-blep) — MIT

`chardsp_ConstantQ.h` and `chardsp_ConstantQStream.h` are derived from
[CiCueTea](https://github.com/jdsierral/CiCueTea) by Juan Sierra — MIT; its
notice is in `chardsp_ConstantQ.h`.

chardsp is ISC-licensed; see [LICENSE](LICENSE). Source distributions must
retain the license files in this repository and its submodules.

Clone recursively:

```sh
git clone --recurse-submodules https://github.com/charCulbert/chardsp.git
```

For an existing checkout:

```sh
git submodule update --init --recursive
```

## Use

Include only the components you need:

```cpp
#include "chardsp/chardsp_Oscillator.h"
#include "chardsp/chardsp_ADSREnvelope.h"
#include "chardsp/chardsp_StateVariableFilter.h"
```

Or add the repository with CMake:

```cmake
add_subdirectory(path/to/chardsp)
target_link_libraries(my_target PRIVATE chardsp::chardsp)
```

## Build

```sh
cmake -S . -B build \
    -DCHARDSP_BUILD_TESTS=ON \
    -DCHARDSP_BUILD_EXAMPLES=ON
cmake --build build
./build/tests/chardsp_tests
```

Interactive examples are currently built on macOS.
