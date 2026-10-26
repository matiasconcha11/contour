#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "PluginProcessor.h"

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    void setParam (ContourProcessor& processor, const juce::String& id, float value)
    {
        auto* param = processor.apvts.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    // Runs a block of constant 1.0 through the processor, so the output is the applied gain.
    juce::AudioBuffer<float> processOnes (ContourProcessor& processor)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), 1.0f, blockSize);

        processor.processBlock (buffer, midi);
        return buffer;
    }

    struct Fixture
    {
        Fixture()
        {
            processor.setRateAndBufferSizeDetails (sampleRate, blockSize);
            processor.prepareToPlay (sampleRate, blockSize);
            setParam (processor, "smooth", 0.0f);
        }

        juce::ScopedJuceInitialiser_GUI juce;
        ContourProcessor processor;
    };
}

TEST_CASE_METHOD (Fixture, "Zero depth leaves audio untouched", "[processor]")
{
    setParam (processor, "depth", 0.0f);
    const auto out = processOnes (processor);

    for (int i = 0; i < blockSize; ++i)
        REQUIRE_THAT (out.getSample (0, i), WithinAbs (1.0f, 1.0e-6));
}

TEST_CASE_METHOD (Fixture, "Flat curve leaves audio untouched", "[processor]")
{
    processor.curve.setPoints (Curve::preset (5));
    const auto out = processOnes (processor);

    for (int i = 0; i < blockSize; ++i)
        REQUIRE_THAT (out.getSample (1, i), WithinAbs (1.0f, 1.0e-6));
}

TEST_CASE_METHOD (Fixture, "Gain follows the curve at full depth", "[processor]")
{
    // A straight ramp from 0 to 1 over one cycle, with a drop back to 0 at the very end.
    processor.curve.setPoints ({ { 0.0f, 0.0f, 0.0f }, { 0.999f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } });
    setParam (processor, "rate", 3.0f); // 1/4 note: one beat per cycle

    // With no host the processor free-runs at 120 bpm from ppq 0, so sample n sits at
    // phase n / samplesPerBeat.
    const auto samplesPerBeat = sampleRate * 60.0 / 120.0;
    const auto out = processOnes (processor);

    CHECK_THAT (out.getSample (0, 0), WithinAbs (0.0f, 1.0e-3));

    for (int i = 64; i < blockSize; i += 64)
        CHECK_THAT (out.getSample (0, i), WithinAbs ((float) (i / samplesPerBeat / 0.999), 2.0e-3));

    CHECK (out.getSample (0, 0) == out.getSample (1, 0)); // both channels get the same gain
}

TEST_CASE_METHOD (Fixture, "Half depth scales the modulation", "[processor]")
{
    processor.curve.setPoints ({ { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } });
    setParam (processor, "depth", 0.5f);
    const auto out = processOnes (processor);

    CHECK_THAT (out.getSample (0, 100), WithinAbs (0.5f, 1.0e-6));
}

TEST_CASE_METHOD (Fixture, "State round trip restores parameters and curve", "[processor]")
{
    const std::vector<CurvePoint> shape { { 0.0f, 0.2f, 0.4f }, { 0.6f, 0.9f, -0.3f }, { 1.0f, 0.2f, 0.0f } };
    processor.curve.setPoints (shape);
    setParam (processor, "depth", 0.37f);
    setParam (processor, "rate", 6.0f);

    juce::MemoryBlock state;
    processor.getStateInformation (state);

    ContourProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());

    CHECK_THAT (restored.apvts.getRawParameterValue ("depth")->load(), WithinAbs (0.37f, 1.0e-4));
    CHECK (restored.apvts.getRawParameterValue ("rate")->load() == 6.0f);

    const auto& points = restored.curve.getPoints();
    REQUIRE (points.size() == shape.size());

    for (size_t i = 0; i < shape.size(); ++i)
    {
        CHECK (points[i].x == shape[i].x);
        CHECK (points[i].y == shape[i].y);
        CHECK (points[i].tension == shape[i].tension);
    }
}

TEST_CASE_METHOD (Fixture, "Garbage state is ignored", "[processor]")
{
    const char junk[] = "definitely not a plugin state";
    processor.setStateInformation (junk, (int) sizeof (junk));

    CHECK (processor.curve.getPoints().size() >= 2);
}
