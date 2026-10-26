#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Curve.h"

using Catch::Matchers::WithinAbs;

TEST_CASE ("Straight segments interpolate linearly", "[curve]")
{
    Curve curve;
    curve.setPoints ({ { 0.0f, 0.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } });

    CHECK_THAT (curve.evaluate (0.0f), WithinAbs (0.0f, 1.0e-6));
    CHECK_THAT (curve.evaluate (0.25f), WithinAbs (0.5f, 1.0e-6));
    CHECK_THAT (curve.evaluate (0.5f), WithinAbs (1.0f, 1.0e-6));
    CHECK_THAT (curve.evaluate (0.75f), WithinAbs (0.5f, 1.0e-6));
}

TEST_CASE ("Tension bends a segment without moving its ends", "[curve]")
{
    Curve curve;
    curve.setPoints ({ { 0.0f, 0.0f, 0.5f }, { 0.5f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } });

    CHECK_THAT (curve.evaluate (0.0f), WithinAbs (0.0f, 1.0e-6));
    CHECK_THAT (curve.evaluate (0.5f), WithinAbs (1.0f, 1.0e-6));
    CHECK (curve.evaluate (0.25f) < 0.5f); // positive tension sags towards the start value

    curve.setPoints ({ { 0.0f, 0.0f, -0.5f }, { 0.5f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } });
    CHECK (curve.evaluate (0.25f) > 0.5f);
}

TEST_CASE ("Points are sanitised", "[curve]")
{
    Curve curve;

    SECTION ("unsorted input is sorted, edges pinned and linked")
    {
        curve.setPoints ({ { 0.7f, 0.2f, 0.0f }, { 0.1f, 0.3f, 0.0f }, { 0.4f, 0.9f, 0.0f } });
        const auto& points = curve.getPoints();

        REQUIRE (points.size() == 3);
        CHECK (points.front().x == 0.0f);
        CHECK (points.back().x == 1.0f);
        CHECK (points[1].x == 0.4f);
        CHECK (points.back().y == points.front().y);
    }

    SECTION ("out of range values are clamped")
    {
        curve.setPoints ({ { -1.0f, 2.0f, 5.0f }, { 3.0f, -1.0f, -5.0f } });
        const auto& points = curve.getPoints();

        CHECK (points.front().y == 1.0f);
        CHECK (points.front().tension == 1.0f);
        CHECK (points.back().tension == -1.0f);
    }

    SECTION ("fewer than two points falls back to flat")
    {
        curve.setPoints ({ { 0.5f, 0.2f, 0.0f } });
        REQUIRE (curve.getPoints().size() == 2);
        CHECK (curve.evaluate (0.5f) == 1.0f);
    }
}

TEST_CASE ("Lookup table matches exact evaluation", "[curve]")
{
    Curve curve;

    for (int preset = 0; preset < Curve::presetNames().size(); ++preset)
    {
        curve.setPoints (Curve::preset (preset));

        for (int i = 0; i <= 1000; ++i)
        {
            const auto x = (float) i / 1000.0f;
            CHECK_THAT (curve.lookup (x), WithinAbs (curve.evaluate (x), 0.02));
        }
    }
}

TEST_CASE ("Every preset loops seamlessly and stays in range", "[curve]")
{
    Curve curve;

    for (int preset = 0; preset < Curve::presetNames().size(); ++preset)
    {
        curve.setPoints (Curve::preset (preset));
        CHECK_THAT (curve.evaluate (0.0f), WithinAbs (curve.evaluate (1.0f), 1.0e-6));

        for (int i = 0; i <= 200; ++i)
        {
            const auto y = curve.evaluate ((float) i / 200.0f);
            CHECK (y >= 0.0f);
            CHECK (y <= 1.0f);
        }
    }
}

TEST_CASE ("Curve survives a ValueTree round trip", "[curve]")
{
    Curve original;
    original.setPoints ({ { 0.0f, 0.3f, -0.2f }, { 0.4f, 0.8f, 0.6f }, { 1.0f, 0.3f, 0.0f } });

    Curve restored;
    restored.fromValueTree (original.toValueTree());

    REQUIRE (restored.getPoints().size() == original.getPoints().size());

    for (size_t i = 0; i < original.getPoints().size(); ++i)
    {
        CHECK (restored.getPoints()[i].x == original.getPoints()[i].x);
        CHECK (restored.getPoints()[i].y == original.getPoints()[i].y);
        CHECK (restored.getPoints()[i].tension == original.getPoints()[i].tension);
    }
}
