#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Curve.h"

namespace Params
{
    // Each module shapes one property of the sound with its own curve.
    // Processing order: time, filter, drive, crush, width, pan, volume.
    enum Module
    {
        volume,
        pan,
        width,
        filter,
        drive,
        crush,
        time,
        numModules
    };

    inline const juce::StringArray moduleIds { "volume", "pan", "width", "filter", "drive", "crush", "time" };
    inline const juce::StringArray moduleNames { "Volume", "Pan", "Width", "Filter", "Drive", "Crush", "Time" };

    inline juce::String onId (int module) { return moduleIds[module] + "On"; }
    inline juce::String depthId (int module) { return moduleIds[module] + "Depth"; }

    // What the curve's vertical axis means, shown in the editor.
    juce::String moduleHint (int module);

    // The curve that does nothing for a module, used by Clear.
    Curve::Shape neutralShape (int module);

    // The curve a fresh instance (and Init) starts with.
    std::vector<CurvePoint> defaultCurve (int module);

    // Pan and Width are neutral in the middle, so the editor draws a centre line.
    inline bool isBipolar (int module) { return module == pan || module == width; }

    enum Mode
    {
        tempoMode,
        hzMode,
        audioMode
    };

    inline const juce::String mode { "mode" };
    inline const juce::String rate { "rate" };
    inline const juce::String rateHz { "rateHz" };
    inline const juce::String threshold { "threshold" };
    inline const juce::String triggerSource { "triggerSource" };
    inline const juce::String offset { "offset" };
    inline const juce::String smooth { "smooth" };
    inline const juce::String filterType { "filterType" };
    inline const juce::String filterResonance { "filterResonance" };
    inline const juce::String mix { "mix" };
    inline const juce::String inputGain { "inputGain" };
    inline const juce::String outputGain { "outputGain" };
    inline const juce::String bypass { "bypass" };

    juce::StringArray modeNames();
    juce::StringArray triggerSourceNames();
    juce::StringArray rateNames();
    juce::StringArray filterTypeNames();
    int defaultRateIndex();

    // Length of one cycle in quarter notes; bar-based rates follow the host's time signature.
    double beatsPerCycle (int rateIndex, double beatsPerBar);

    // Number of bars in one cycle for bar-based rates ("1 bar", "2 bars"...), 0 for note values.
    int barsPerCycle (int rateIndex);

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
