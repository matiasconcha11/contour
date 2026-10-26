#include "Parameters.h"

namespace Params
{
    namespace
    {
        struct Rate
        {
            const char* name;
            double beats; // quarter notes; negative means that many bars
        };

        constexpr Rate rates[] = {
            { "1/32", 0.125 },     { "1/16T", 1.0 / 6.0 }, { "1/16", 0.25 },     { "1/8T", 1.0 / 3.0 },
            { "1/16D", 0.375 },    { "1/8", 0.5 },         { "1/4T", 2.0 / 3.0 }, { "1/8D", 0.75 },
            { "1/4", 1.0 },        { "1/2T", 4.0 / 3.0 },  { "1/4D", 1.5 },       { "1/2", 2.0 },
            { "1/2D", 3.0 },       { "1 bar", -1.0 },      { "2 bars", -2.0 },    { "4 bars", -4.0 },
            { "8 bars", -8.0 },
        };

        juce::String percent (float value, int)
        {
            return juce::String (juce::roundToInt (value * 100.0f)) + "%";
        }

        juce::String decibels (float value, int)
        {
            return (value > 0.0f ? "+" : "") + juce::String (value, 1) + " dB";
        }

        juce::AudioParameterFloatAttributes percentAttributes()
        {
            return juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction (percent)
                .withValueFromStringFunction ([] (const juce::String& text) { return text.getFloatValue() / 100.0f; });
        }
    }

    juce::String moduleHint (int module)
    {
        switch (module)
        {
            case volume: return "Curve sets the level";
            case pan: return "Bottom is left, top is right";
            case width: return "Bottom is mono, centre is unchanged, top is extra wide";
            case filter: return "Curve sets the cutoff";
            case drive: return "Curve sets the saturation";
            case crush: return "Curve sets bit depth and sample-rate reduction";
            case time: return "Lower reads further back: slopes slow down or reverse, steps repeat";
            default: return {};
        }
    }

    Curve::Shape neutralShape (int module)
    {
        switch (module)
        {
            case pan:
            case width: return Curve::flatCentre;
            case drive:
            case crush: return Curve::flatBottom;
            default: return Curve::flatTop;
        }
    }

    std::vector<CurvePoint> defaultCurve (int module)
    {
        switch (module)
        {
            case volume: return Curve::preset (Curve::duck);
            case filter: return Curve::preset (Curve::sine);
            case drive: return Curve::preset (Curve::rampDown);
            case crush: return Curve::preset (Curve::rampUp);
            case time: return Curve::preset (Curve::tapeStop);
            default: return Curve::preset (Curve::sine);
        }
    }

    juce::StringArray modeNames()
    {
        return { "Tempo", "Hz", "Audio" };
    }

    juce::StringArray filterTypeNames()
    {
        return { "Low-pass", "High-pass", "Band-pass" };
    }

    juce::StringArray rateNames()
    {
        juce::StringArray names;

        for (const auto& r : rates)
            names.add (r.name);

        return names;
    }

    int defaultRateIndex()
    {
        return rateNames().indexOf ("1/4");
    }

    double beatsPerCycle (int rateIndex, double beatsPerBar)
    {
        const auto& r = rates[juce::jlimit (0, (int) std::size (rates) - 1, rateIndex)];
        return r.beats > 0.0 ? r.beats : -r.beats * beatsPerBar;
    }

    int barsPerCycle (int rateIndex)
    {
        const auto& r = rates[juce::jlimit (0, (int) std::size (rates) - 1, rateIndex)];
        return r.beats < 0.0 ? (int) -r.beats : 0;
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;

        AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { mode, 1 }, "Mode", modeNames(), tempoMode));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { rate, 1 }, "Rate", rateNames(), defaultRateIndex()));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { rateHz, 1 }, "Rate (Hz)", NormalisableRange<float> (0.05f, 20.0f, 0.0f, 0.3f), 1.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, v < 1.0f ? 2 : 1) + " Hz"; })));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { threshold, 1 }, "Trigger Threshold", NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -18.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { offset, 1 }, "Offset",
                                                           NormalisableRange<float> (0.0f, 1.0f), 0.0f, percentAttributes()));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { smooth, 1 }, "Smooth", NormalisableRange<float> (0.0f, 50.0f, 0.0f, 0.4f), 1.5f,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " ms"; })));

        for (int m = 0; m < numModules; ++m)
        {
            layout.add (std::make_unique<AudioParameterBool> (ParameterID { onId (m), 1 }, moduleNames[m] + " On", m == volume));
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { depthId (m), 1 }, moduleNames[m] + " Depth",
                                                               NormalisableRange<float> (0.0f, 1.0f), 1.0f, percentAttributes()));
        }

        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { filterType, 1 }, "Filter Type", filterTypeNames(), 0));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { filterResonance, 1 }, "Filter Resonance",
                                                           NormalisableRange<float> (0.0f, 1.0f), 0.3f, percentAttributes()));

        const auto gainAttributes = AudioParameterFloatAttributes().withStringFromValueFunction (decibels);
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { inputGain, 1 }, "Input",
                                                           NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, gainAttributes));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { mix, 1 }, "Mix",
                                                           NormalisableRange<float> (0.0f, 1.0f), 1.0f, percentAttributes()));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { outputGain, 1 }, "Output",
                                                           NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, gainAttributes));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { bypass, 1 }, "Bypass", false));

        return layout;
    }
}
