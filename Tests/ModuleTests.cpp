#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "PluginProcessor.h"

#include <cmath>

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 480;

    // A host transport we can start and stop.
    struct TestPlayHead : juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override { return info; }
        PositionInfo info;
    };

    struct Fixture
    {
        Fixture()
        {
            processor.setRateAndBufferSizeDetails (sampleRate, blockSize);
            set (Params::smooth, 0.0f);
            processor.prepareToPlay (sampleRate, blockSize);
        }

        void set (const juce::String& id, float value)
        {
            auto* param = processor.apvts.getParameter (id);
            param->setValueNotifyingHost (param->convertTo0to1 (value));
        }

        // Turns one module on with a constant curve and everything else off.
        void solo (int module, float curveValue, float depth = 1.0f)
        {
            for (int m = 0; m < Params::numModules; ++m)
                set (Params::onId (m), m == module ? 1.0f : 0.0f);

            processor.curves[(size_t) module].setPoints ({ { 0.0f, curveValue, 0.0f }, { 1.0f, curveValue, 0.0f } });
            set (Params::depthId (module), depth);
        }

        // Processes `numBlocks` blocks produced by `source(sampleIndex)`, returning the left channel.
        std::vector<float> run (int numBlocks, const std::function<float (int)>& source)
        {
            std::vector<float> out;
            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;

            for (int b = 0; b < numBlocks; ++b)
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    const auto v = source (b * blockSize + i);
                    buffer.setSample (0, i, v);
                    buffer.setSample (1, i, v);
                }

                processor.processBlock (buffer, midi);
                out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
            }

            return out;
        }

        static float rms (const std::vector<float>& signal, size_t from)
        {
            double sum = 0.0;

            for (auto i = from; i < signal.size(); ++i)
                sum += (double) signal[i] * signal[i];

            return (float) std::sqrt (sum / (double) (signal.size() - from));
        }

        juce::ScopedJuceInitialiser_GUI juce;
        ContourProcessor processor;
    };

    float sine (int n, double frequency) { return (float) std::sin (juce::MathConstants<double>::twoPi * frequency * n / sampleRate); }
}

TEST_CASE_METHOD (Fixture, "Tempo mode follows the host and freezes when it stops", "[transport]")
{
    TestPlayHead host;
    host.info.setBpm (120.0);
    host.info.setIsPlaying (true);
    host.info.setPpqPosition (0.25);
    processor.setPlayHead (&host);

    run (1, [] (int) { return 0.0f; });
    const auto playingPhase = processor.displayPhase.load();
    CHECK (playingPhase > 0.25f); // a 1/4 cycle started a quarter of the way in, then moved on

    host.info.setIsPlaying (false);
    run (1, [] (int) { return 0.0f; });
    const auto stoppedPhase = processor.displayPhase.load();
    run (5, [] (int) { return 0.0f; });

    CHECK (processor.displayPhase.load() == stoppedPhase);
}

TEST_CASE_METHOD (Fixture, "Without a host the shape runs on its own", "[transport]")
{
    run (1, [] (int) { return 0.0f; });
    const auto first = processor.displayPhase.load();
    run (1, [] (int) { return 0.0f; });

    CHECK (processor.displayPhase.load() != first);
}

TEST_CASE_METHOD (Fixture, "Audio mode restarts the cycle on each hit", "[transport]")
{
    set (Params::mode, (float) Params::audioMode);
    set (Params::threshold, -20.0f);

    // Silence, then a loud burst starting at sample 4800.
    run (20, [] (int n) { return n >= 4800 && n < 4900 ? 0.9f : 0.0f; });

    CHECK (processor.triggerCount.load() == 1);

    // The phase restarted at the hit: 9600 - 4800 samples in, at 120 bpm (24000 samples per 1/4).
    CHECK_THAT (processor.displayPhase.load(), WithinAbs ((9600.0f - 4800.0f) / 24000.0f, 2.0e-3));
}

TEST_CASE_METHOD (Fixture, "Quiet audio never triggers", "[transport]")
{
    set (Params::mode, (float) Params::audioMode);
    set (Params::threshold, -20.0f);
    run (20, [] (int n) { return 0.05f * sine (n, 220.0); });

    CHECK (processor.triggerCount.load() == 0);
}

TEST_CASE_METHOD (Fixture, "Filter at the bottom of the curve removes highs", "[modules]")
{
    solo (Params::filter, 0.0f);
    const auto out = run (20, [] (int n) { return sine (n, 5000.0); });

    CHECK (rms (out, 4800) < 0.01f); // more than 37 dB down
}

TEST_CASE_METHOD (Fixture, "Filter at the top of the curve lets everything through", "[modules]")
{
    solo (Params::filter, 1.0f);
    const auto out = run (20, [] (int n) { return sine (n, 1000.0); });

    CHECK_THAT (rms (out, 4800), WithinAbs (0.7071f, 0.02));
}

TEST_CASE_METHOD (Fixture, "High-pass at the bottom of the curve is transparent", "[modules]")
{
    set (Params::filterType, 1.0f);
    solo (Params::filter, 0.0f);
    const auto out = run (20, [] (int n) { return sine (n, 1000.0); });

    CHECK_THAT (rms (out, 4800), WithinAbs (0.7071f, 0.02));
}

TEST_CASE_METHOD (Fixture, "Drive and Crush at zero are exactly dry", "[modules]")
{
    for (auto module : { (int) Params::drive, (int) Params::crush })
    {
        solo (module, 0.0f);
        const auto out = run (2, [] (int n) { return 0.5f * sine (n, 440.0); });

        for (int n = 0; n < (int) out.size(); ++n)
            REQUIRE_THAT (out[(size_t) n], WithinAbs (0.5f * sine (n, 440.0), 1.0e-6));
    }
}

TEST_CASE_METHOD (Fixture, "Drive saturates loud signals", "[modules]")
{
    solo (Params::drive, 1.0f);
    const auto out = run (4, [] (int n) { return 0.9f * sine (n, 100.0); });

    const auto peak = std::abs (*std::max_element (out.begin(), out.end(), [] (float a, float b) { return std::abs (a) < std::abs (b); }));
    CHECK (peak < 0.5f); // tanh flattens the peaks, loudness compensation pulls the level down
}

TEST_CASE_METHOD (Fixture, "Crush quantises and holds samples", "[modules]")
{
    solo (Params::crush, 1.0f);
    const auto out = run (4, [] (int n) { return 0.6f * sine (n, 50.0); });

    // Full amount: 3 bits and a 1/40 sample rate, so long runs of identical samples.
    int repeats = 0;

    for (size_t n = 1; n < out.size(); ++n)
        repeats += out[n] == out[n - 1] ? 1 : 0;

    CHECK (repeats > (int) out.size() * 9 / 10);
}

TEST_CASE_METHOD (Fixture, "Time at the top of the curve is exactly dry", "[modules]")
{
    solo (Params::time, 1.0f);
    const auto out = run (2, [] (int n) { return sine (n, 300.0); });

    for (int n = 0; n < (int) out.size(); ++n)
        REQUIRE_THAT (out[(size_t) n], WithinAbs (sine (n, 300.0), 1.0e-6));
}

TEST_CASE_METHOD (Fixture, "Time halfway down delays by half a cycle", "[modules]")
{
    solo (Params::time, 0.5f);

    // 1/4 at 120 bpm is 24000 samples, so the delay is 12000.
    const auto out = run (60, [] (int n) { return n == 100 ? 1.0f : 0.0f; });

    CHECK_THAT (out[12100], WithinAbs (1.0f, 1.0e-4));
    CHECK_THAT (out[100], WithinAbs (0.0f, 1.0e-6));
}

TEST_CASE_METHOD (Fixture, "Sessions from version 0.2 keep their sync setting", "[processor]")
{
    // 0.2 had a "sync" switch where 0.3 has a mode choice.
    juce::ValueTree state ("PARAMS");
    juce::ValueTree sync ("PARAM");
    sync.setProperty ("id", "sync", nullptr).setProperty ("value", 0.0f, nullptr);
    state.appendChild (sync, nullptr);

    juce::MemoryBlock data;
    juce::AudioProcessor::copyXmlToBinary (*state.createXml(), data);
    processor.setStateInformation (data.getData(), (int) data.getSize());

    CHECK ((int) processor.apvts.getRawParameterValue (Params::mode)->load() == Params::hzMode);
}
