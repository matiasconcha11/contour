# Contour

A ShaperBox-style volume shaper: draw a curve, and Contour applies it as gain, synced to the host tempo.
JUCE 9, builds AU + VST3 + Standalone.

## Build

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

AU and VST3 bundles are copied to `~/Library/Audio/Plug-Ins/` after each build.

## Editing the curve

- drag a point to move it (the edge points stay linked so the loop is seamless)
- drag between points to bend the segment
- double-click to add a point, double-click or right-click a point to remove it
- hold Cmd while dragging to turn off grid snapping
