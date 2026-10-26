#include "CurveEditor.h"
#include "LookAndFeel.h"

namespace Colours
{
    const juce::Colour background { 0xff101115 };
    const juce::Colour gridMinor { 0xff191b20 };
    const juce::Colour gridMajor { 0xff252830 };
    const juce::Colour point = Theme::text;
    const juce::Colour playhead = Theme::text;
}

namespace
{
    constexpr float pointRadius = 5.0f;
    constexpr float hitRadius = 9.0f;
}

CurveEditor::CurveEditor (Curve& curveToEdit, const Waveform& input, const Waveform& output)
    : curve (&curveToEdit), waveIn (input), waveOut (output)
{
    setRepaintsOnMouseActivity (false);
}

void CurveEditor::setCurve (Curve& curveToEdit, juce::Colour newAccent, bool showCentreLine)
{
    curve = &curveToEdit;
    accent = newAccent;
    centreLine = showCentreLine;
    hoverPoint = dragPoint = bendSegment = -1;
    repaint();
}

void CurveEditor::setActive (bool isActive)
{
    active = isActive;
    repaint();
}

void CurveEditor::setPlayhead (float phase)
{
    // Repaint every frame: the waveform behind the curve keeps changing.
    playhead = phase;
    repaint();
}

void CurveEditor::setGridDivisions (int divisions)
{
    gridDivisions = juce::jmax (1, divisions);
    repaint();
}

juce::Rectangle<float> CurveEditor::plotArea() const
{
    return getLocalBounds().toFloat().reduced (10.0f);
}

juce::Point<float> CurveEditor::toScreen (float x, float y) const
{
    const auto area = plotArea();
    return { area.getX() + x * area.getWidth(), area.getBottom() - y * area.getHeight() };
}

juce::Point<float> CurveEditor::toCurve (juce::Point<float> screen) const
{
    const auto area = plotArea();
    return { juce::jlimit (0.0f, 1.0f, (screen.x - area.getX()) / area.getWidth()),
             juce::jlimit (0.0f, 1.0f, (area.getBottom() - screen.y) / area.getHeight()) };
}

int CurveEditor::pointAt (juce::Point<float> screen) const
{
    const auto& points = curve->getPoints();

    // The last point mirrors the first, so it is only grabbable on its own side.
    for (int i = (int) points.size() - 1; i >= 0; --i)
        if (toScreen (points[(size_t) i].x, points[(size_t) i].y).getDistanceFrom (screen) <= hitRadius)
            return i;

    return -1;
}

int CurveEditor::segmentAt (float curveX) const
{
    const auto& points = curve->getPoints();

    for (size_t i = 0; i + 1 < points.size(); ++i)
        if (curveX <= points[i + 1].x)
            return (int) i;

    return (int) points.size() - 2;
}

float CurveEditor::snapX (float x, const juce::ModifierKeys& mods) const
{
    if (mods.isCommandDown())
        return x;

    return std::round (x * (float) gridDivisions) / (float) gridDivisions;
}

void CurveEditor::commit (std::vector<CurvePoint> points)
{
    curve->setPoints (std::move (points));
    repaint();

    if (onChange)
        onChange();
}

void CurveEditor::paint (juce::Graphics& g)
{
    const auto area = plotArea();
    const auto colour = active ? accent : Theme::faint;

    g.setColour (Colours::background);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    for (int i = 0; i <= gridDivisions; ++i)
    {
        const auto x = area.getX() + area.getWidth() * (float) i / (float) gridDivisions;
        const auto major = gridDivisions >= 4 && i % (gridDivisions / 4) == 0;
        g.setColour (major ? Colours::gridMajor : Colours::gridMinor);
        g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
    }

    for (int i = 0; i <= 4; ++i)
    {
        g.setColour (i == 0 || i == 4 ? Colours::gridMajor : Colours::gridMinor);
        g.drawHorizontalLine (juce::roundToInt (area.getY() + area.getHeight() * (float) i / 4.0f),
                              area.getX(), area.getRight());
    }

    if (centreLine)
    {
        g.setColour (Theme::faint);
        const auto y = area.getCentreY();
        const float dashes[] { 4.0f, 4.0f };
        g.drawDashedLine ({ area.getX(), y, area.getRight(), y }, dashes, 2, 1.0f);
    }

    // Audio over one cycle: input as a dim silhouette, output in the module colour.
    const auto binWidth = area.getWidth() / (float) waveformBins;

    for (int i = 0; i < waveformBins; ++i)
    {
        const auto x = area.getX() + (float) i * binWidth;
        const auto in = std::sqrt (juce::jlimit (0.0f, 1.0f, waveIn[(size_t) i].load (std::memory_order_relaxed)));
        const auto out = std::sqrt (juce::jlimit (0.0f, 1.0f, waveOut[(size_t) i].load (std::memory_order_relaxed)));

        g.setColour (Theme::text.withAlpha (0.06f));
        g.fillRect (x, area.getBottom() - in * area.getHeight(), binWidth, in * area.getHeight());
        g.setColour (colour.withAlpha (0.22f));
        g.fillRect (x, area.getBottom() - out * area.getHeight(), binWidth, out * area.getHeight());
    }

    juce::Path line;
    const auto steps = juce::jmax (2, (int) area.getWidth());

    for (int i = 0; i <= steps; ++i)
    {
        const auto x = (float) i / (float) steps;
        const auto p = toScreen (x, curve->evaluate (x));

        if (i == 0)
            line.startNewSubPath (p);
        else
            line.lineTo (p);
    }

    juce::Path fill (line);
    fill.lineTo (area.getBottomRight());
    fill.lineTo (area.getBottomLeft());
    fill.closeSubPath();

    g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.28f), area.getTopLeft(),
                                             colour.withAlpha (0.02f), area.getBottomLeft(), false));
    g.fillPath (fill);

    g.setColour (colour.withAlpha (0.25f));
    g.strokePath (line, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (colour);
    g.strokePath (line, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (playhead >= 0.0f)
    {
        const auto head = toScreen (playhead, curve->evaluate (playhead));
        g.setColour (Colours::playhead.withAlpha (0.25f));
        g.drawVerticalLine (juce::roundToInt (head.x), area.getY(), area.getBottom());
        g.setColour (colour.withAlpha (0.35f));
        g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (head));
        g.setColour (Colours::playhead);
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (head));
    }

    const auto& points = curve->getPoints();

    for (size_t i = 0; i < points.size(); ++i)
    {
        const auto centre = toScreen (points[i].x, points[i].y);
        const auto isActive = (int) i == hoverPoint || (int) i == dragPoint;
        const auto radius = isActive ? pointRadius + 1.5f : pointRadius;

        g.setColour (Colours::background);
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));
        g.setColour (isActive ? colour : Colours::point);
        g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre), 2.0f);
    }
}

void CurveEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto hovered = pointAt (e.position);

    if (hovered != hoverPoint)
    {
        hoverPoint = hovered;
        repaint();
    }

    setMouseCursor (hovered >= 0 ? juce::MouseCursor::DraggingHandCursor
                                 : juce::MouseCursor::UpDownResizeCursor);
}

void CurveEditor::mouseExit (const juce::MouseEvent&)
{
    hoverPoint = -1;
    repaint();
}

void CurveEditor::mouseDown (const juce::MouseEvent& e)
{
    dragPoint = pointAt (e.position);
    bendSegment = -1;

    const auto& points = curve->getPoints();
    const auto lastIndex = (int) points.size() - 1;

    if (e.mods.isPopupMenu())
    {
        if (dragPoint > 0 && dragPoint < lastIndex)
        {
            auto edited = points;
            edited.erase (edited.begin() + dragPoint);
            commit (std::move (edited));
        }

        dragPoint = -1;
        return;
    }

    if (dragPoint < 0)
    {
        bendSegment = segmentAt (toCurve (e.position).x);
        bendStartTension = points[(size_t) bendSegment].tension;
        bendStartY = e.position.y;
    }
}

void CurveEditor::mouseDrag (const juce::MouseEvent& e)
{
    auto points = curve->getPoints();
    const auto lastIndex = (int) points.size() - 1;

    if (dragPoint >= 0)
    {
        const auto target = toCurve (e.position);
        auto& p = points[(size_t) dragPoint];

        // Edge points share one value; dragging the last one moves the first.
        if (dragPoint == lastIndex)
            points.front().y = target.y;
        else
            p.y = target.y;

        if (dragPoint > 0 && dragPoint < lastIndex)
            p.x = juce::jlimit (points[(size_t) dragPoint - 1].x, points[(size_t) dragPoint + 1].x,
                                snapX (target.x, e.mods));

        commit (std::move (points));
        return;
    }

    if (bendSegment >= 0)
    {
        const auto& a = points[(size_t) bendSegment];
        const auto& b = points[(size_t) bendSegment + 1];
        const auto dragUp = (bendStartY - e.position.y) / plotArea().getHeight();

        // Dragging up always bows the line upwards, whichever way the segment slopes.
        const auto direction = b.y >= a.y ? -1.0f : 1.0f;
        points[(size_t) bendSegment].tension = juce::jlimit (-1.0f, 1.0f, bendStartTension + direction * dragUp * 2.0f);

        commit (std::move (points));
    }
}

void CurveEditor::mouseUp (const juce::MouseEvent&)
{
    dragPoint = -1;
    bendSegment = -1;
    repaint();
}

void CurveEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto points = curve->getPoints();
    const auto hit = pointAt (e.position);

    if (hit > 0 && hit < (int) points.size() - 1)
    {
        points.erase (points.begin() + hit);
        commit (std::move (points));
        return;
    }

    if (hit >= 0)
        return;

    const auto target = toCurve (e.position);
    const auto x = snapX (target.x, e.mods);

    if (x <= 0.0f || x >= 1.0f)
        return;

    const auto segment = segmentAt (x);
    points.insert (points.begin() + segment + 1, { x, target.y, 0.0f });
    commit (std::move (points));
}
