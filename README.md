# chardsp

`chardsp` is a small header-only collection of audio DSP primitives. Its
initial API contains a finite linear smoother and a Signalsmith elliptic-BLEP
saw oscillator. It uses CHOC buffer views directly in block-oriented callers.

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
```

Or add the repository and link its usage requirements:

```cmake
add_subdirectory(path/to/chardsp)
target_link_libraries(my_executable PRIVATE chardsp)
```

`chardsp` is an `INTERFACE` target. It builds no reusable static or dynamic
library.

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
