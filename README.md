# Contour

A ShaperBox-style shaper: draw a curve per module and Contour applies it in time with the host.
JUCE 9, builds AU + VST3 + Standalone.

- **Modules:** Volume, Pan, Width, Filter (LP/HP/BP + resonance), Drive, Crush and Time (tape stop,
  stutter, reverse), each with its own curve, on/off and depth
- **Timing:** Tempo (follows the host, freezes when it stops), Hz (free-running) or Audio (restarts the curve
  on each hit above a threshold); rates from 1/32 to 8 bars incl. triplets and dotted; offset; smoothing
- **Editing:** shapes, invert, reverse, copy/paste between modules and instances, undo/redo, grid
- **Output:** input gain, dry/wet mix, output gain with meter, click-free bypass
- **Presets:** 23 factory presets in categories; user presets saved as `.contourpreset` files in
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
CI (`.github/workflows/ci.yml`) builds on macOS (unit tests, pluginval, auval) and Windows (unit tests) on every push.

## Releases

Push a version tag (`git tag v0.3.0 && git push --tags`) and `.github/workflows/release.yml` builds a universal
macOS AU + VST3 and a Windows VST3, and attaches the zips to a draft GitHub release. The macOS builds are not
notarized: that needs an Apple Developer account, without which other Macs will block the plugin.

Before a release, also check by hand in Logic, Ableton and Reaper: tempo changes and looping,
parameter automation, sample rate / buffer size changes, offline bounce vs realtime, and session reload.

## Editing the curve

- drag a point to move it (the edge points stay linked so the loop is seamless)
- drag between points to bend the segment
- double-click to add a point, double-click or right-click a point to remove it
- hold Cmd while dragging to turn off grid snapping
