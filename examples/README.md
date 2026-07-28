# chardsp examples

Each example is one source file and demonstrates one component directly. The
examples deliberately avoid a shared framework so the complete use of each DSP
type is visible in one place.

| Target | Demonstrates | Output |
|---|---|---|
| `chardsp_oscillators` | Naive, PolyBLEP, elliptic, and Nomad oscillator choices | WAV |
| `chardsp_adsr` | ADSR stages and velocity-scaled envelope level | WAV |
| `chardsp_filter` | Lowpass, bandpass, and highpass SVF modes | WAV |
| `chardsp_noise` | Brown, pink, white, and blue noise | WAV |
| `chardsp_downsampler` | Explicit external 4x rendering and downsampling | WAV |
| `chardsp_delay_line` | Fixed-capacity feedback delay with separate read/write | WAV |
| `chardsp_oversampler` | Signalsmith-backed 4x nonlinear block processing | WAV |
| `chardsp_voice_pool` | Two voices, release-aware stealing, and pending starts | WAV + trace |
| `chardsp_mono_portamento` | Last-note priority, legato, and portamento | WAV |
| `chardsp_basic_synth_voice` | Complete voice integration | WAV |
| `chardsp_buffer_render` | Focused elliptic saw plus gain smoothing | WAV |

Build everything:

```sh
cmake -S . -B build -DCHARDSP_BUILD_EXAMPLES=ON
cmake --build build
```

Every audio example accepts an optional output path. For example:

```sh
./build/examples/adsr/chardsp_adsr adsr.wav
./build/examples/voicePool/chardsp_voice_pool stealing.wav
```
