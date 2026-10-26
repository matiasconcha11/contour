#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace Params
{
    // Each module shapes one property of the sound with its own curve.
    enum Module
    {
        volume,
        pan,
        width,
        numModules
    };

    inline const juce::StringArray moduleIds { "volume", "pan", "width" };
    inline const juce::StringArray moduleNames { "Volume", "Pan", "Width" };

    inline juce::String onId (int module) { return moduleIds[module] + "On"; }
    inline juce::String depthId (int module) { return moduleIds[module] + "Depth"; }

    inline const juce::String rate { "rate" };
    inline const juce::String rateHz { "rateHz" };
    inline const juce::String sync { "sync" };
    inline const juce::String offset { "offset" };
    inline const juce::String smooth { "smooth" };
    inline const juce::String mix { "mix" };
    inline const juce::String inputGain { "inputGain" };
    inline const juce::String outputGain { "outputGain" };
    inline const juce::String bypass { "bypass" };

    juce::StringArray rateNames();
    int defaultRateIndex();

    // Length of one cycle in quarter notes; bar-based rates follow the host's time signature.
    double beatsPerCycle (int rateIndex, double beatsPerBar);

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
