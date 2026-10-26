#include "Curve.h"

#include <algorithm>
#include <cmath>

namespace
{
    float shapeSegment (float t, float tension)
    {
        // Exponent bend: tension > 0 sags towards the start value, < 0 rushes to the end value.
        const auto exponent = std::pow (2.0f, tension * 4.0f);
        return std::pow (t, exponent);
    }
}

Curve::Curve()
{
    setPoints (preset (0));
}

void Curve::setPoints (std::vector<CurvePoint> newPoints)
{
    points = std::move (newPoints);
    sanitise();
    rebuildTable();
}

void Curve::sanitise()
{
    if (points.size() < 2)
        points = { { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } };

    for (auto& p : points)
    {
        p.x = std::clamp (p.x, 0.0f, 1.0f);
        p.y = std::clamp (p.y, 0.0f, 1.0f);
        p.tension = std::clamp (p.tension, -1.0f, 1.0f);
    }

    std::stable_sort (points.begin(), points.end(),
                      [] (const auto& a, const auto& b) { return a.x < b.x; });

    // Edges are pinned and share a value so the cycle loops without a jump.
    points.front().x = 0.0f;
    points.back().x = 1.0f;
    points.back().y = points.front().y;
}

float Curve::evaluate (float x) const
{
    x = std::clamp (x, 0.0f, 1.0f);

    for (size_t i = 0; i + 1 < points.size(); ++i)
    {
        const auto& a = points[i];
        const auto& b = points[i + 1];

        if (x <= b.x)
        {
            const auto width = b.x - a.x;
            const auto t = width > 0.0f ? (x - a.x) / width : 1.0f;
            return a.y + (b.y - a.y) * shapeSegment (t, a.tension);
        }
    }

    return points.back().y;
}

void Curve::rebuildTable()
{
    for (int i = 0; i <= tableSize; ++i)
        table[(size_t) i].store (evaluate ((float) i / (float) tableSize), std::memory_order_relaxed);
}

float Curve::lookup (float phase) const noexcept
{
    const auto pos = std::clamp (phase, 0.0f, 1.0f) * (float) tableSize;
    const auto index = std::min ((int) pos, tableSize - 1);
    const auto frac = pos - (float) index;
    const auto a = table[(size_t) index].load (std::memory_order_relaxed);
    const auto b = table[(size_t) index + 1].load (std::memory_order_relaxed);
    return a + (b - a) * frac;
}

juce::ValueTree Curve::toValueTree() const
{
    juce::ValueTree tree ("CURVE");

    for (const auto& p : points)
    {
        juce::ValueTree node ("POINT");
        node.setProperty ("x", p.x, nullptr);
        node.setProperty ("y", p.y, nullptr);
        node.setProperty ("tension", p.tension, nullptr);
        tree.appendChild (node, nullptr);
    }

    return tree;
}

void Curve::fromValueTree (const juce::ValueTree& tree)
{
    if (! tree.hasType ("CURVE"))
        return;

    std::vector<CurvePoint> loaded;

    for (const auto& node : tree)
        loaded.push_back ({ (float) node.getProperty ("x"),
                            (float) node.getProperty ("y"),
                            (float) node.getProperty ("tension") });

    setPoints (std::move (loaded));
}

juce::StringArray Curve::presetNames()
{
    return { "Duck", "Pump", "Ramp Up", "Gate 1/2", "Sine", "Flat" };
}

std::vector<CurvePoint> Curve::preset (int index)
{
    switch (index)
    {
        case 0: // Duck: classic sidechain dip
            return { { 0.0f, 0.0f, -0.6f }, { 0.35f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
        case 1: // Pump: smoother, longer recovery
            return { { 0.0f, 0.15f, -0.35f }, { 0.6f, 1.0f, 0.0f }, { 0.9f, 1.0f, 0.5f }, { 1.0f, 0.15f, 0.0f } };
        case 2: // Ramp up
            return { { 0.0f, 0.0f, 0.3f }, { 0.97f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
        case 3: // Gate on the first half
            return { { 0.0f, 1.0f, 0.0f }, { 0.48f, 1.0f, 0.0f }, { 0.5f, 0.0f, 0.0f },
                     { 0.98f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } };
        case 4: // Sine-ish
            return { { 0.0f, 0.5f, -0.4f }, { 0.25f, 1.0f, 0.4f }, { 0.5f, 0.5f, -0.4f },
                     { 0.75f, 0.0f, 0.4f }, { 1.0f, 0.5f, 0.0f } };
        default: // Flat
            return { { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } };
    }
}
