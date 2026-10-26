#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <atomic>
#include <vector>

// A periodic shape over one LFO cycle. x and y are both normalised to [0, 1].
// Each point owns the tension of the segment that leaves it (0 = straight line).
struct CurvePoint
{
    float x = 0.0f;
    float y = 1.0f;
    float tension = 0.0f; // [-1, 1]
};

class Curve
{
public:
    static constexpr int tableSize = 2048;

    enum Shape
    {
        duck,
        pump,
        rampUp,
        rampDown,
        triangle,
        sine,
        square,
        tranceGate,
        stairs,
        tapeStop,
        flatTop,
        flatCentre,
        flatBottom,
        numShapes
    };

    Curve();

    // Message thread only.
    const std::vector<CurvePoint>& getPoints() const { return points; }
    void setPoints (std::vector<CurvePoint> newPoints);
    float evaluate (float x) const; // exact evaluation, used for drawing

    void invert();
    void reverse();

    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree& tree);

    // Audio thread safe: reads the lookup table built from the points.
    float lookup (float phase) const noexcept;

    static std::vector<CurvePoint> preset (int shape);
    static juce::StringArray presetNames();

    // Square steps from a pattern such as "x.x.xx.x": 'x' is high, anything else low.
    static std::vector<CurvePoint> gate (const juce::String& pattern, float high = 1.0f, float low = 0.0f);

private:
    void sanitise();
    void rebuildTable();

    std::vector<CurvePoint> points;
    std::array<std::atomic<float>, tableSize + 1> table;
};
