#include "PluginEditor.h"

namespace
{
    const juce::Colour panel { 0xff0e0f12 };
    const juce::Colour text { 0xfff4f1ea };
    const juce::Colour dim { 0xff8a8d96 };
    const juce::Colour accent { 0xffffb347 };
}

ContourEditor::ContourEditor (ContourProcessor& p)
    : AudioProcessorEditor (&p), processor (p), curveEditor (p.curve)
{
    lookAndFeel.setColour (juce::Slider::rotarySliderFillColourId, accent);
    lookAndFeel.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff2c2f38));
    lookAndFeel.setColour (juce::Slider::thumbColourId, text);
    lookAndFeel.setColour (juce::Slider::textBoxTextColourId, text);
    lookAndFeel.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lookAndFeel.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff1f2127));
    lookAndFeel.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff2c2f38));
    lookAndFeel.setColour (juce::ComboBox::textColourId, text);
    lookAndFeel.setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1f2127));
    lookAndFeel.setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.3f));
    setLookAndFeel (&lookAndFeel);

    curveEditor.onChange = [this] { processor.curveChanged(); };
    addAndMakeVisible (curveEditor);

    presetBox.addItemList (Curve::presetNames(), 1);
    presetBox.setTextWhenNothingSelected ("Shapes");
    presetBox.onChange = [this]
    {
        const auto index = presetBox.getSelectedItemIndex();

        if (index < 0)
            return;

        processor.curve.setPoints (Curve::preset (index));
        processor.curveChanged();
        curveEditor.repaint();
        presetBox.setSelectedId (0, juce::dontSendNotification);
    };
    addAndMakeVisible (presetBox);

    rateBox.addItemList (ContourProcessor::rateNames(), 1);
    rateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, "rate", rateBox);
    addAndMakeVisible (rateBox);

    setUpKnob (depth, "depth", "Depth");
    setUpKnob (smooth, "smooth", "Smooth");
    setUpKnob (offset, "offset", "Offset");

    setResizable (true, true);
    setResizeLimits (520, 320, 1600, 1000);
    setSize (760, 420);

    startTimerHz (60);
}

ContourEditor::~ContourEditor()
{
    setLookAndFeel (nullptr);
}

void ContourEditor::setUpKnob (Knob& knob, const juce::String& paramId, const juce::String& name)
{
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
    knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, paramId, knob.slider);
    addAndMakeVisible (knob.slider);

    knob.label.setText (name.toUpperCase(), juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    knob.label.setColour (juce::Label::textColourId, dim);
    addAndMakeVisible (knob.label);
}

void ContourEditor::paint (juce::Graphics& g)
{
    g.fillAll (panel);

    auto header = getLocalBounds().removeFromTop (44).reduced (16, 0);
    g.setColour (text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("CONTOUR", header, juce::Justification::centredLeft);

    g.setColour (dim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("double-click: add/remove point   drag between points: bend   cmd: no snap",
                getLocalBounds().removeFromBottom (22).reduced (16, 0), juce::Justification::centredLeft);
}

void ContourEditor::resized()
{
    auto bounds = getLocalBounds();

    auto header = bounds.removeFromTop (44).reduced (16, 8);
    rateBox.setBounds (header.removeFromRight (100));
    header.removeFromRight (8);
    presetBox.setBounds (header.removeFromRight (120));

    bounds.removeFromBottom (22);

    auto controls = bounds.removeFromRight (110).reduced (8, 4);
    const auto knobHeight = controls.getHeight() / 3;

    for (auto* knob : { &depth, &smooth, &offset })
    {
        auto area = controls.removeFromTop (knobHeight);
        knob->label.setBounds (area.removeFromTop (16));
        knob->slider.setBounds (area);
    }

    curveEditor.setBounds (bounds.reduced (16, 4).withTrimmedRight (-8));
}

void ContourEditor::timerCallback()
{
    curveEditor.setPlayhead (processor.displayPhase.load (std::memory_order_relaxed));
}

void ContourEditor::curveReloaded()
{
    curveEditor.repaint();
}
