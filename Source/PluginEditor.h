#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "CurveEditor.h"
#include "LookAndFeel.h"
#include "PluginProcessor.h"
#include "PresetManager.h"

// Inline prompt for naming or confirming, drawn over the editor (no separate windows inside a host).
class PresetDialog : public juce::Component
{
public:
    PresetDialog();

    void showPrompt (const juce::String& title, const juce::String& initialText, const juce::String& confirmText,
                     std::function<void (const juce::String&)> onConfirm);
    void showConfirm (const juce::String& title, const juce::String& confirmText, std::function<void()> onConfirm);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void confirm();
    void close();
    juce::Rectangle<int> panelBounds() const;

    juce::String title;
    juce::TextEditor nameEditor;
    juce::TextButton okButton, cancelButton { "Cancel" };
    std::function<void (const juce::String&)> onConfirmText;
    bool wantsText = false;
};

class ContourEditor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    explicit ContourEditor (ContourProcessor&);
    ~ContourEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Called after the host restores a session.
    void stateReloaded();

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct ModuleTab
    {
        juce::TextButton tab;
        juce::ToggleButton power;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;
    };

    void timerCallback() override;
    void setUpKnob (Knob&, const juce::String& paramId, const juce::String& name, bool bipolar = false);
    void selectModule (int module);
    void curveEdited();

    void showPresetMenu();
    void savePreset();
    void savePresetAs();
    void refreshPresetLabel();

    void showShapesMenu();

    ContourProcessor& processor;
    PresetManager presets;
    ContourLookAndFeel lookAndFeel;

    int currentModule = Params::volume;

    // Header
    juce::TextButton prevButton { "<" }, presetButton, nextButton { ">" }, saveButton { "Save" };
    juce::TextButton bypassButton { "Bypass" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    // Modules and curve tools
    std::array<ModuleTab, Params::numModules> tabs;
    juce::TextButton shapesButton { "Shapes" }, invertButton { "Invert" }, reverseButton { "Reverse" }, clearButton { "Clear" };
    juce::ComboBox gridBox;
    CurveEditor curveEditor;

    // Controls
    Knob rate, rateHz, offset, smooth, depth, input, mix, output;
    juce::TextButton syncButton { "Sync" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAttachment;

    PresetDialog dialog;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ContourEditor)
};
