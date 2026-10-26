#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// Sidebar listing every module with its power switch and a live thumbnail of its curve.
class ModuleStrip : public juce::Component
{
public:
    explicit ModuleStrip (ContourProcessor&);

    std::function<void (int)> onSelect;
    void setSelected (int module);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> rowBounds (int module) const;
    int rowAt (juce::Point<int>) const;

    ContourProcessor& processor;
    std::array<juce::ToggleButton, Params::numModules> powers;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, Params::numModules> powerAttachments;
    int selected = 0;
    int hovered = -1;
};

// Stereo peak meter with a slow fall-off.
class LevelMeter : public juce::Component
{
public:
    void setLevels (float left, float right);
    void paint (juce::Graphics&) override;

private:
    float levels[2] {};
};

// Row of buttons acting as one choice parameter (e.g. Tempo | Hz | Audio).
class ChoiceSelector : public juce::Component
{
public:
    ChoiceSelector (juce::RangedAudioParameter&, const juce::StringArray& labels);

    void setTooltip (const juce::String&);

    void resized() override;

private:
    juce::OwnedArray<juce::TextButton> buttons;
    juce::ParameterAttachment attachment;
};
