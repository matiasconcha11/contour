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

    // Runs one block of constant (left, right) through the processor.
    juce::AudioBuffer<float> process (ContourProcessor& processor, float left = 1.0f, float right = 1.0f)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        juce::FloatVectorOperations::fill (buffer.getWritePointer (0), left, blockSize);
        juce::FloatVectorOperations::fill (buffer.getWritePointer (1), right, blockSize);
        processor.processBlock (buffer, midi);
        return buffer;
    }

    struct Fixture
    {
        Fixture()
        {
            processor.setRateAndBufferSizeDetails (sampleRate, blockSize);
            setParam (processor, Params::smooth, 0.0f);
            processor.prepareToPlay (sampleRate, blockSize);
        }

        void useConstantCurve (int module, float value)
        {
            processor.curves[(size_t) module].setPoints ({ { 0.0f, value, 0.0f }, { 1.0f, value, 0.0f } });
            setParam (processor, Params::onId (module), 1.0f);
        }

        juce::ScopedJuceInitialiser_GUI juce;
        ContourProcessor processor;
    };
}

TEST_CASE_METHOD (Fixture, "Zero depth leaves audio untouched", "[processor]")
{
    setParam (processor, Params::depthId (Params::volume), 0.0f);
    const auto out = process (processor, 0.5f, -0.25f);

    for (int i = 0; i < blockSize; ++i)
    {
        REQUIRE_THAT (out.getSample (0, i), WithinAbs (0.5f, 1.0e-6));
        REQUIRE_THAT (out.getSample (1, i), WithinAbs (-0.25f, 1.0e-6));
    }
}

TEST_CASE_METHOD (Fixture, "Gain follows the volume curve at full depth", "[processor]")
{
    processor.curves[Params::volume].setPoints ({ { 0.0f, 0.0f, 0.0f }, { 0.999f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } });
    setParam (processor, Params::rate, (float) Params::rateNames().indexOf ("1/4"));

    // With no host the processor free-runs at 120 bpm from ppq 0.
    const auto samplesPerBeat = sampleRate * 60.0 / 120.0;
    const auto out = process (processor);

    CHECK_THAT (out.getSample (0, 0), WithinAbs (0.0f, 1.0e-3));

    for (int i = 64; i < blockSize; i += 64)
        CHECK_THAT (out.getSample (0, i), WithinAbs ((float) (i / samplesPerBeat / 0.999), 2.0e-3));

    CHECK (out.getSample (0, 0) == out.getSample (1, 0));
}

TEST_CASE_METHOD (Fixture, "Volume depth scales the modulation", "[processor]")
{
    useConstantCurve (Params::volume, 0.0f);
    setParam (processor, Params::depthId (Params::volume), 0.5f);

    CHECK_THAT (process (processor).getSample (0, 100), WithinAbs (0.5f, 1.0e-5));
}

TEST_CASE_METHOD (Fixture, "Pan moves the signal between channels", "[processor]")
{
    setParam (processor, Params::onId (Params::volume), 0.0f);

    SECTION ("centre is unity on both sides")
    {
        useConstantCurve (Params::pan, 0.5f);
        const auto out = process (processor);
        CHECK_THAT (out.getSample (0, 100), WithinAbs (1.0f, 1.0e-5));
        CHECK_THAT (out.getSample (1, 100), WithinAbs (1.0f, 1.0e-5));
    }

    SECTION ("hard left silences the right channel")
    {
        useConstantCurve (Params::pan, 0.0f);
        const auto out = process (processor);
        CHECK (out.getSample (0, 100) > 1.0f);
        CHECK_THAT (out.getSample (1, 100), WithinAbs (0.0f, 1.0e-5));
    }

    SECTION ("hard right silences the left channel")
    {
        useConstantCurve (Params::pan, 1.0f);
        const auto out = process (processor);
        CHECK_THAT (out.getSample (0, 100), WithinAbs (0.0f, 1.0e-5));
        CHECK (out.getSample (1, 100) > 1.0f);
    }
}

TEST_CASE_METHOD (Fixture, "Width scales the side signal", "[processor]")
{
    setParam (processor, Params::onId (Params::volume), 0.0f);

    SECTION ("bottom of the curve collapses to mono")
    {
        useConstantCurve (Params::width, 0.0f);
        const auto out = process (processor, 1.0f, 0.0f);
        CHECK_THAT (out.getSample (0, 100), WithinAbs (0.5f, 1.0e-5));
        CHECK_THAT (out.getSample (1, 100), WithinAbs (0.5f, 1.0e-5));
    }

    SECTION ("centre of the curve leaves the image alone")
    {
        useConstantCurve (Params::width, 0.5f);
        const auto out = process (processor, 1.0f, 0.0f);
        CHECK_THAT (out.getSample (0, 100), WithinAbs (1.0f, 1.0e-5));
        CHECK_THAT (out.getSample (1, 100), WithinAbs (0.0f, 1.0e-5));
    }

    SECTION ("top of the curve doubles the side signal")
    {
        useConstantCurve (Params::width, 1.0f);
        const auto out = process (processor, 1.0f, 0.0f);
        CHECK_THAT (out.getSample (0, 100), WithinAbs (1.5f, 1.0e-5));
        CHECK_THAT (out.getSample (1, 100), WithinAbs (-0.5f, 1.0e-5));
    }
}

TEST_CASE_METHOD (Fixture, "Mix at zero passes the dry signal", "[processor]")
{
    useConstantCurve (Params::volume, 0.0f);
    setParam (processor, Params::mix, 0.0f);
    setParam (processor, Params::inputGain, 12.0f);
    processor.prepareToPlay (sampleRate, blockSize); // jump smoothers to their targets

    const auto out = process (processor, 0.3f, 0.3f);
    CHECK_THAT (out.getSample (0, 100), WithinAbs (0.3f, 1.0e-5));
}

TEST_CASE_METHOD (Fixture, "Output gain applies after the mix", "[processor]")
{
    setParam (processor, Params::depthId (Params::volume), 0.0f);
    setParam (processor, Params::outputGain, -6.0f);
    processor.prepareToPlay (sampleRate, blockSize);

    CHECK_THAT (process (processor).getSample (0, 100), WithinAbs (juce::Decibels::decibelsToGain (-6.0f), 1.0e-5));
}

TEST_CASE_METHOD (Fixture, "Bypass fades to the dry signal", "[processor]")
{
    useConstantCurve (Params::volume, 0.0f);
    setParam (processor, Params::bypass, 1.0f);

    for (int block = 0; block < 4; ++block) // let the 30 ms crossfade finish
        process (processor);

    CHECK_THAT (process (processor).getSample (0, blockSize - 1), WithinAbs (1.0f, 1.0e-5));
}

TEST_CASE_METHOD (Fixture, "Free-running Hz mode follows its own rate", "[processor]")
{
    setParam (processor, Params::sync, 0.0f);
    setParam (processor, Params::rateHz, 2.0f);
    process (processor);

    const auto expected = (float) (2.0 * blockSize / sampleRate);
    CHECK_THAT (processor.displayPhase.load(), WithinAbs (expected, 1.0e-3));
}

TEST_CASE_METHOD (Fixture, "State round trip restores parameters, curves and preset name", "[processor]")
{
    const std::vector<CurvePoint> shape { { 0.0f, 0.2f, 0.4f }, { 0.6f, 0.9f, -0.3f }, { 1.0f, 0.2f, 0.0f } };
    processor.curves[Params::width].setPoints (shape);
    setParam (processor, Params::depthId (Params::pan), 0.37f);
    setParam (processor, Params::rate, 6.0f);
    processor.presetName = "My Groove";

    juce::MemoryBlock state;
    processor.getStateInformation (state);

    ContourProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());

    CHECK_THAT (restored.apvts.getRawParameterValue (Params::depthId (Params::pan))->load(), WithinAbs (0.37f, 1.0e-4));
    CHECK (restored.apvts.getRawParameterValue (Params::rate)->load() == 6.0f);
    CHECK (restored.presetName == "My Groove");

    const auto& points = restored.curves[Params::width].getPoints();
    REQUIRE (points.size() == shape.size());

    for (size_t i = 0; i < shape.size(); ++i)
    {
        CHECK (points[i].x == shape[i].x);
        CHECK (points[i].y == shape[i].y);
        CHECK (points[i].tension == shape[i].tension);
    }
}

TEST_CASE_METHOD (Fixture, "Sessions from version 0.1 still load", "[processor]")
{
    // 0.1 stored a single CURVE child next to the parameters.
    juce::ValueTree state ("PARAMS");
    juce::ValueTree curve ("CURVE");

    for (const auto& [x, y] : { std::pair { 0.0f, 0.3f }, std::pair { 0.5f, 0.8f }, std::pair { 1.0f, 0.3f } })
    {
        juce::ValueTree point ("POINT");
        point.setProperty ("x", x, nullptr).setProperty ("y", y, nullptr).setProperty ("tension", 0.0f, nullptr);
        curve.appendChild (point, nullptr);
    }

    state.appendChild (curve, nullptr);
    juce::MemoryBlock data;
    juce::AudioProcessor::copyXmlToBinary (*state.createXml(), data);
    processor.setStateInformation (data.getData(), (int) data.getSize());

    REQUIRE (processor.curves[Params::volume].getPoints().size() == 3);
    CHECK (processor.curves[Params::volume].getPoints()[1].y == 0.8f);
}

TEST_CASE_METHOD (Fixture, "Garbage state is ignored", "[processor]")
{
    const char junk[] = "definitely not a plugin state";
    processor.setStateInformation (junk, (int) sizeof (junk));

    CHECK (processor.curves[Params::volume].getPoints().size() >= 2);
}
