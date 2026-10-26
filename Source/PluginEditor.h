#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Components.h"
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
    bool keyPressed (const juce::KeyPress&) override;

    // Called after the host restores a session.
    void stateReloaded();

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void timerCallback() override;
    void setUpKnob (Knob&, const juce::String& paramId, const juce::String& name, bool bipolar = false);
    void selectModule (int module);

    // Curve edits go through the undo manager, one step per gesture or button press.
    void beginCurveEdit();
    void endCurveEdit();
    void editCurve (const std::function<void (Curve&)>& change);
    void copyCurve();
    void pasteCurve();

    void showPresetMenu();
    void savePreset();
    void savePresetAs();
    void refreshPresetLabel();
    void showShapesMenu();

    ContourProcessor& processor;
    PresetManager presets;
    ContourLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    int currentModule = Params::volume;
    std::vector<CurvePoint> pointsBeforeEdit;

    // Header
    juce::TextButton prevButton { "<" }, presetButton, nextButton { ">" }, saveButton { "Save" };
    juce::TextButton bypassButton { "Bypass" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    // Modules and curve
    ModuleStrip moduleStrip;
    juce::TextButton shapesButton { "Shapes" }, invertButton { "Invert" }, reverseButton { "Reverse" },
        copyButton { "Copy" }, pasteButton { "Paste" }, clearButton { "Clear" }, undoButton { "Undo" }, redoButton { "Redo" };
    juce::ComboBox gridBox;
    CurveEditor curveEditor;

    // Timing
    Knob rate, rateHz, offset, smooth, threshold;
    ChoiceSelector modeSelector, sourceSelector;

    // Module
    Knob depth, resonance;
    juce::ComboBox filterTypeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> filterTypeAttachment;

    // Output
    Knob input, mix, output;
    LevelMeter meter;

    PresetDialog dialog;

    // Layout regions remembered for painting group captions.
    juce::Rectangle<int> timingGroup, moduleGroup, outputGroup;
    int lastTriggerCount = 0;
    int frameCount = 0;
    float triggerFlash = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ContourEditor)
};
