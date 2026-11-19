# Glint

A stereo variable-mu tube compressor plugin (VST3 + Standalone) with its own
character: a "sparkle" air generator that opens up as the compressor works.
Two channels, six time-constant positions, stereo-linked or Mid/Side
(Lateral/Vertical) operation.

## Controls

| Control | Range | Default | Notes |
|---|---|---|---|
| Input | -20..+20 dB | 0 dB | drives the detector and the tube stage |
| Threshold | 0..10 | 3 | detector threshold, -6 dBFS (0) .. -36 dBFS (10), 3 dB per step |
| Time | 1..6 | 2 | time constants: 1: 0.2 ms/0.3 s, 2: 0.2/0.8, 3: 0.4/2.0, 4: 0.8/5.0, 5: 0.2/program dependent (2 s + 10 s parallel), 6: 0.4/program dependent (0.3 s, 10 s, 25 s) |
| Link | Stereo, Mid/Side | Stereo | Stereo = one detector on max(|L|,|R|); Mid/Side = two independent channels |
| Sparkle | 0..100 % | 30 % | air-band harmonic generator, amount grows with gain reduction |
| Mix | 0..100 % | 100 % | dry/wet, latency-aligned dry path |
| Output | -20..+20 dB | 0 dB | |

The gain cell is a feedback design (the detector reads the cell's output):
soft knee, no fixed ratio — the ratio rises smoothly with level. The tube +
transformer colour stage runs inside 2x oversampling (linear-phase FIR
half-band, integer latency); the reported latency is compensated in the dry
path. Current gain reduction (dB) is exposed atomically for a future meter.

## Build (Linux)

Requires CMake >= 3.22, Ninja, a C++20 compiler. JUCE 8.0.17-security is
fetched automatically.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j6
ctest --test-dir build
```

## Check

`scripts/check.sh` builds, runs the unit tests, and validates the VST3 with
pluginval (strictness 5):

```sh
./scripts/check.sh
```
