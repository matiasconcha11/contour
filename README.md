# Contour

A ShaperBox-style volume shaper: draw a curve, and Contour applies it as gain, synced to the host tempo.
JUCE 9, builds AU + VST3 + Standalone.

## Build and test

```
make            # Release build; AU + VST3 are copied to ~/Library/Audio/Plug-Ins/
make debug      # Debug build in build-debug/ (JUCE assertions on)
make test       # Catch2 unit tests (curve maths, gain, state save/restore)
make validate   # pluginval at strictness 10 on the VST3 and AU
make check      # test + validate
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
