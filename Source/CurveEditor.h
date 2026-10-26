#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Curve.h"

// Interactive editor for a Curve.
//   drag point        move it (edge points move vertically, and stay linked)
//   drag between      bend the segment
//   double-click      add a point, or remove the one under the mouse
//   right-click point remove it
//   hold Cmd          disable grid snapping
class CurveEditor : public juce::Component
{
public:
    explicit CurveEditor (Curve& curveToEdit);

    std::function<void()> onChange;

    void setPlayhead (float phase);
    void setGridDivisions (int divisions);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> plotArea() const;
    juce::Point<float> toScreen (float x, float y) const;
    juce::Point<float> toCurve (juce::Point<float> screen) const;
    int pointAt (juce::Point<float> screen) const;
    int segmentAt (float curveX) const;
    float snapX (float x, const juce::ModifierKeys& mods) const;
    void commit (std::vector<CurvePoint> points);

    Curve& curve;
    float playhead = -1.0f;
    int gridDivisions = 16;

    int hoverPoint = -1;
    int dragPoint = -1;
    int bendSegment = -1;
    float bendStartTension = 0.0f;
    float bendStartY = 0.0f;
};
