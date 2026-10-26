# Contour

A ShaperBox-style shaper: draw a curve per module and Contour applies it in time with the host.
JUCE 9, builds AU + VST3 + Standalone.

- **Modules:** Volume, Pan (equal-power) and Width (mid/side), each with its own curve, on/off and depth
- **Timing:** tempo-synced rates from 1/32 to 8 bars incl. triplets and dotted, or free Hz; offset; smoothing
- **Output:** input gain, dry/wet mix, output gain, click-free bypass
- **Presets:** 13 factory presets; user presets saved as `.contourpreset` files in
  `~/Library/Audio/Presets/Matias Concha/Contour/` (falls back to `~/Documents/Contour Presets` if that isn't writable)

## Build and test

```
make            # Release build; AU + VST3 are copied to ~/Library/Audio/Plug-Ins/
make debug      # Debug build in build-debug/ (JUCE assertions on)
make test       # Catch2 unit tests (curve maths, gain, state save/restore)
make validate   # pluginval at strictness 10 on the VST3 and AU
make check      # test + validate

./build/ContourTests "[.snapshot]"   # render the editor to /tmp/contour.png (CONTOUR_SNAPSHOT_PRESET=n for a preset)
```

Requires Xcode, CMake and Ninja (`brew install cmake ninja`). The first build downloads JUCE and Catch2.
CI (`.github/workflows/ci.yml`) runs the build, unit tests, pluginval and auval on every push.

Before a release, also check by hand in Logic, Ableton and Reaper: tempo changes and looping,
parameter automation, sample rate / buffer size changes, offline bounce vs realtime, and session reload.

## Editing the curve

- drag a point to move it (the edge points stay linked so the loop is seamless)
- drag between points to bend the segment
- double-click to add a point, double-click or right-click a point to remove it
- hold Cmd while dragging to turn off grid snapping
