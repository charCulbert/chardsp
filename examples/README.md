# chardsp examples

Each example is its own interactive executable. `common/DemoHost.h/.cpp` owns
the shared CHOC audio device, MIDI input, window, WebView, and basic control
plumbing. Each example source owns only its DSP path, actions, and controls.

The available demos are:

- oscillators
- ADSR envelope
- state-variable filter
- colored noise
- 4x downsampler
- delay line
- block oversampler
- voice pool
- mono portamento
- polyphonic basic synth

Build everything:

```sh
cmake -S . -B build -DCHARDSP_BUILD_EXAMPLES=ON
cmake --build build
```

On macOS, run any example directly:

```sh
./build/examples/chardsp_adsr
./build/examples/chardsp_oscillators
./build/examples/chardsp_basic_synth_voice
```
