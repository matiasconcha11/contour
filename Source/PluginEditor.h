#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "CurveEditor.h"
#include "PluginProcessor.h"

class ContourEditor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    explicit ContourEditor (ContourProcessor&);
    ~ContourEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Called after the host restores a session.
    void curveReloaded();

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void timerCallback() override;
    void setUpKnob (Knob&, const juce::String& paramId, const juce::String& name);

    ContourProcessor& processor;
    juce::LookAndFeel_V4 lookAndFeel;

    CurveEditor curveEditor;
    juce::ComboBox presetBox, rateBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> rateAttachment;
    Knob depth, smooth, offset;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ContourEditor)
};
