#include "PluginEditor.h"

namespace
{
    constexpr int headerHeight = 56;
    constexpr int tabRowHeight = 44;
    constexpr int controlsHeight = 140;
    constexpr int margin = 16;

    void styleSmallButton (juce::TextButton& button, juce::Colour accent)
    {
        button.setColour (juce::TextButton::buttonOnColourId, accent);
    }
}

//==============================================================================
PresetDialog::PresetDialog()
{
    setWantsKeyboardFocus (true);

    nameEditor.setFont (Theme::font (15.0f));
    nameEditor.setJustification (juce::Justification::centredLeft);
    nameEditor.setIndents (8, 6);
    nameEditor.onReturnKey = [this] { confirm(); };
    nameEditor.onEscapeKey = [this] { close(); };
    addChildComponent (nameEditor);

    okButton.onClick = [this] { confirm(); };
    cancelButton.onClick = [this] { close(); };
    okButton.setToggleState (true, juce::dontSendNotification);
    addAndMakeVisible (okButton);
    addAndMakeVisible (cancelButton);
}

void PresetDialog::showPrompt (const juce::String& newTitle, const juce::String& initialText,
                               const juce::String& confirmText, std::function<void (const juce::String&)> onConfirm)
{
    title = newTitle;
    wantsText = true;
    onConfirmText = std::move (onConfirm);
    okButton.setButtonText (confirmText);
    nameEditor.setText (initialText, false);
    nameEditor.setVisible (true);
    nameEditor.selectAll();
    setVisible (true);
    toFront (true);
    resized();
    nameEditor.grabKeyboardFocus();
}

void PresetDialog::showConfirm (const juce::String& newTitle, const juce::String& confirmText, std::function<void()> onConfirm)
{
    title = newTitle;
    wantsText = false;
    onConfirmText = [onConfirm] (const juce::String&) { onConfirm(); };
    okButton.setButtonText (confirmText);
    nameEditor.setVisible (false);
    setVisible (true);
    toFront (true);
    resized();
    grabKeyboardFocus();
}

void PresetDialog::confirm()
{
    const auto text = nameEditor.getText().trim();

    if (wantsText && text.isEmpty())
        return;

    auto callback = std::move (onConfirmText);
    close();

    if (callback)
        callback (text);
}

void PresetDialog::close()
{
    onConfirmText = nullptr;
    setVisible (false);
}

juce::Rectangle<int> PresetDialog::panelBounds() const
{
    return getLocalBounds().withSizeKeepingCentre (340, wantsText ? 150 : 120);
}

void PresetDialog::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.6f));

    const auto panel = panelBounds().toFloat();
    g.setColour (Theme::raised);
    g.fillRoundedRectangle (panel, 8.0f);
    g.setColour (Theme::outline);
    g.drawRoundedRectangle (panel, 8.0f, 1.0f);

    g.setColour (Theme::text);
    g.setFont (Theme::font (15.0f, true));
    g.drawFittedText (title, panelBounds().reduced (18, 14).removeFromTop (24), juce::Justification::centredLeft, 1);
}

void PresetDialog::resized()
{
    auto panel = panelBounds().reduced (18, 14);
    panel.removeFromTop (30);

    if (wantsText)
    {
        nameEditor.setBounds (panel.removeFromTop (34));
        panel.removeFromTop (14);
    }

    auto buttons = panel.removeFromBottom (30);
    okButton.setBounds (buttons.removeFromRight (100));
    buttons.removeFromRight (8);
    cancelButton.setBounds (buttons.removeFromRight (90));
}

bool PresetDialog::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
        close();
    else if (key == juce::KeyPress::returnKey)
        confirm();
    else
        return false;

    return true;
}

void PresetDialog::mouseDown (const juce::MouseEvent& e)
{
    if (! panelBounds().contains (e.getPosition()))
        close();
}

//==============================================================================
ContourEditor::ContourEditor (ContourProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      presets (p),
      curveEditor (p.curves[Params::volume], p.waveIn, p.waveOut)
{
    setLookAndFeel (&lookAndFeel);

    // Header: presets and bypass.
    for (auto* button : { &prevButton, &nextButton, &saveButton, &presetButton })
        addAndMakeVisible (button);

    prevButton.onClick = [this] { presets.step (-1); stateReloaded(); };
    nextButton.onClick = [this] { presets.step (1); stateReloaded(); };
    presetButton.onClick = [this] { showPresetMenu(); };
    saveButton.onClick = [this] { savePreset(); };

    bypassButton.setClickingTogglesState (true);
    styleSmallButton (bypassButton, juce::Colour (0xffff6b6b));
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, Params::bypass, bypassButton);
    addAndMakeVisible (bypassButton);

    // Module tabs, each with its own power switch.
    for (int m = 0; m < Params::numModules; ++m)
    {
        auto& t = tabs[(size_t) m];
        t.tab.setButtonText (Params::moduleNames[m]);
        t.tab.setRadioGroupId (1);
        t.tab.setClickingTogglesState (true);
        styleSmallButton (t.tab, Theme::modules[m]);
        t.tab.onClick = [this, m] { selectModule (m); };
        addAndMakeVisible (t.tab);

        t.power.setColour (juce::ToggleButton::tickColourId, Theme::modules[m]);
        t.power.setTooltip ("Turn " + Params::moduleNames[m] + " on or off");
        t.powerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, Params::onId (m), t.power);
        addAndMakeVisible (t.power);
    }

    // Curve tools.
    for (auto* button : { &shapesButton, &invertButton, &reverseButton, &clearButton })
        addAndMakeVisible (button);

    shapesButton.onClick = [this] { showShapesMenu(); };
    invertButton.onClick = [this] { processor.curves[(size_t) currentModule].invert(); curveEdited(); };
    reverseButton.onClick = [this] { processor.curves[(size_t) currentModule].reverse(); curveEdited(); };
    clearButton.onClick = [this]
    {
        const auto flat = currentModule == Params::volume ? Curve::flatTop : Curve::flatCentre;
        processor.curves[(size_t) currentModule].setPoints (Curve::preset (flat));
        curveEdited();
    };

    gridBox.addItemList ({ "Grid 1/4", "Grid 1/8", "Grid 1/16", "Grid 1/32", "Grid Off" }, 1);
    gridBox.onChange = [this]
    {
        static constexpr int divisions[] { 4, 8, 16, 32, 1 };
        curveEditor.setGridDivisions (divisions[juce::jlimit (0, 4, gridBox.getSelectedItemIndex())]);
    };
    gridBox.setSelectedItemIndex (2);
    addAndMakeVisible (gridBox);

    curveEditor.onChange = [this] { curveEdited(); };
    addAndMakeVisible (curveEditor);

    // Controls.
    setUpKnob (rate, Params::rate, "Rate");
    setUpKnob (rateHz, Params::rateHz, "Rate");
    setUpKnob (offset, Params::offset, "Offset");
    setUpKnob (smooth, Params::smooth, "Smooth");
    setUpKnob (depth, Params::depthId (Params::volume), "Depth");
    setUpKnob (input, Params::inputGain, "Input", true);
    setUpKnob (mix, Params::mix, "Mix");
    setUpKnob (output, Params::outputGain, "Output", true);

    syncButton.setClickingTogglesState (true);
    syncButton.setTooltip ("Lock the rate to the host tempo");
    syncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, Params::sync, syncButton);
    addAndMakeVisible (syncButton);

    addChildComponent (dialog);

    selectModule (Params::volume);
    timerCallback(); // set up visibility before the first paint

    setResizable (true, true);
    setResizeLimits (760, 500, 1800, 1200);
    setSize (920, 600);

    startTimerHz (60);
}

ContourEditor::~ContourEditor()
{
    setLookAndFeel (nullptr);
}

void ContourEditor::setUpKnob (Knob& knob, const juce::String& paramId, const juce::String& name, bool bipolar)
{
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    knob.slider.getProperties().set ("bipolar", bipolar);
    knob.slider.setColour (juce::Slider::rotarySliderFillColourId, Theme::text.withAlpha (0.85f));
    knob.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    knob.slider.setColour (juce::Slider::textBoxTextColourId, Theme::text);
    knob.slider.lookAndFeelChanged();
    knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, paramId, knob.slider);
    addAndMakeVisible (knob.slider);

    knob.label.setText (name.toUpperCase(), juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setFont (Theme::caps (10.5f));
    knob.label.setColour (juce::Label::textColourId, Theme::dim);
    addAndMakeVisible (knob.label);
}

void ContourEditor::selectModule (int module)
{
    currentModule = module;
    tabs[(size_t) module].tab.setToggleState (true, juce::dontSendNotification);

    const auto accent = Theme::modules[module];
    curveEditor.setCurve (processor.curves[(size_t) module], accent, module != Params::volume);

    depth.attachment.reset();
    depth.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, Params::depthId (module), depth.slider);
    depth.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    depth.label.setText (Params::moduleNames[module].toUpperCase() + " DEPTH", juce::dontSendNotification);

    for (auto* button : { &shapesButton, &invertButton, &reverseButton, &clearButton })
        styleSmallButton (*button, accent);

    repaint();
}

void ContourEditor::curveEdited()
{
    curveEditor.repaint();
    processor.curveChanged();
}

//==============================================================================
void ContourEditor::refreshPresetLabel()
{
    const auto text = processor.presetName + (processor.isPresetDirty() ? " *" : "");

    if (presetButton.getButtonText() != text)
        presetButton.setButtonText (text);
}

void ContourEditor::showPresetMenu()
{
    presets.refresh();

    juce::PopupMenu menu;
    const auto& list = presets.getPresets();
    const auto current = presets.getCurrentIndex();
    const auto userStart = PresetManager::numFactoryPresets();

    menu.addSectionHeader ("Factory");

    for (int i = 0; i < userStart; ++i)
        menu.addItem (i + 1, list[(size_t) i].name, true, i == current);

    menu.addSectionHeader ("User");

    if ((int) list.size() == userStart)
        menu.addItem (-1, "No saved presets yet", false, false);

    for (int i = userStart; i < (int) list.size(); ++i)
        menu.addItem (i + 1, list[(size_t) i].name, true, i == current);

    constexpr int saveId = 10000, saveAsId = 10001, deleteId = 10002, revealId = 10003, initId = 10004;
    menu.addSeparator();
    menu.addItem (saveId, "Save");
    menu.addItem (saveAsId, "Save As...");
    menu.addItem (deleteId, "Delete \"" + processor.presetName + "\"...", presets.isCurrentUserPreset());
    menu.addSeparator();
    menu.addItem (initId, "Reset to Init");
    menu.addItem (revealId, "Show Presets Folder");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton).withMinimumWidth (presetButton.getWidth()),
                        [this, current] (int result)
                        {
                            if (result <= 0)
                                return;

                            switch (result)
                            {
                                case saveId: savePreset(); break;
                                case saveAsId: savePresetAs(); break;
                                case initId: presets.loadInit(); stateReloaded(); break;
                                case revealId:
                                    presets.getUserDirectory().createDirectory();
                                    presets.getUserDirectory().revealToUser();
                                    break;
                                case deleteId:
                                    dialog.showConfirm ("Delete \"" + processor.presetName + "\"?", "Delete",
                                                        [this, current] { presets.remove (current); refreshPresetLabel(); });
                                    break;
                                default:
                                    presets.load (result - 1);
                                    stateReloaded();
                                    break;
                            }
                        });
}

void ContourEditor::savePreset()
{
    // Saving over a factory preset isn't allowed, so that becomes Save As.
    if (presets.isCurrentUserPreset())
    {
        presets.save (processor.presetName);
        refreshPresetLabel();
    }
    else
    {
        savePresetAs();
    }
}

void ContourEditor::savePresetAs()
{
    const auto suggestion = presets.isCurrentUserPreset() ? processor.presetName : processor.presetName + " (copy)";

    dialog.showPrompt ("Save preset", suggestion, "Save", [this] (const juce::String& name)
    {
        if (! presets.save (name))
            dialog.showConfirm ("Couldn't save to " + presets.getUserDirectory().getFullPathName(), "OK", [] {});

        refreshPresetLabel();
    });
}

void ContourEditor::showShapesMenu()
{
    juce::PopupMenu menu;
    const auto names = Curve::presetNames();

    for (int i = 0; i < names.size(); ++i)
        menu.addItem (i + 1, names[i]);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&shapesButton), [this] (int result)
    {
        if (result <= 0)
            return;

        processor.curves[(size_t) currentModule].setPoints (Curve::preset (result - 1));
        curveEdited();
    });
}

//==============================================================================
void ContourEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    const auto bounds = getLocalBounds();

    // Header bar.
    auto header = bounds.withHeight (headerHeight);
    g.setColour (Theme::panel);
    g.fillRect (header);
    g.setColour (Theme::outline);
    g.drawHorizontalLine (headerHeight - 1, 0.0f, (float) getWidth());

    auto logo = header.reduced (margin, 0);
    g.setColour (Theme::text);
    g.setFont (Theme::caps (20.0f).withExtraKerningFactor (0.3f));
    g.drawText ("CONTOUR", logo, juce::Justification::centredLeft);
    g.setColour (Theme::dim);
    g.setFont (Theme::font (11.0f));
    g.drawText ("v" CONTOUR_VERSION, logo.withTrimmedLeft (148), juce::Justification::centredLeft);

    // Controls panel.
    const auto controls = bounds.withTop (getHeight() - controlsHeight);
    g.setColour (Theme::panel);
    g.fillRect (controls);
    g.setColour (Theme::outline);
    g.drawHorizontalLine (controls.getY(), 0.0f, (float) getWidth());

    // Group dividers between timing | module | output knobs.
    for (const auto x : { (smooth.slider.getRight() + depth.slider.getX()) / 2, (depth.slider.getRight() + input.slider.getX()) / 2 })
        g.drawVerticalLine (x, (float) controls.getY() + 18.0f, (float) controls.getBottom() - 18.0f);
}

void ContourEditor::resized()
{
    auto bounds = getLocalBounds();
    dialog.setBounds (bounds);

    // Header: logo on the left, preset bar centred, bypass on the right.
    auto header = bounds.removeFromTop (headerHeight).reduced (margin, 12);
    bypassButton.setBounds (header.removeFromRight (76));

    auto presetBar = header.withSizeKeepingCentre (juce::jmin (420, header.getWidth() - 320), header.getHeight());
    saveButton.setBounds (presetBar.removeFromRight (64));
    presetBar.removeFromRight (6);
    nextButton.setBounds (presetBar.removeFromRight (32));
    presetBar.removeFromRight (4);
    prevButton.setBounds (presetBar.removeFromLeft (32));
    presetBar.removeFromLeft (4);
    presetButton.setBounds (presetBar);

    // Controls at the bottom: timing | module depth | output stage.
    auto controls = bounds.removeFromBottom (controlsHeight).reduced (margin, 10);
    const auto knobWidth = juce::jmin (96, (controls.getWidth() - 60) / 8);

    // Every knob reserves the same strip underneath, which Rate uses for its Sync switch.
    const auto placeKnob = [knobWidth] (Knob& knob, juce::Rectangle<int>& row)
    {
        auto area = row.removeFromLeft (knobWidth);
        knob.label.setBounds (area.removeFromTop (16));
        area.removeFromBottom (22);
        knob.slider.setBounds (area);
        return area;
    };

    const auto rateArea = placeKnob (rate, controls);
    rateHz.label.setBounds (rate.label.getBounds());
    rateHz.slider.setBounds (rateArea);
    syncButton.setBounds (rateArea.getX(), rateArea.getBottom() + 2, rateArea.getWidth(), 20);
    syncButton.setBounds (syncButton.getBounds().withSizeKeepingCentre (56, 20));

    placeKnob (offset, controls);
    placeKnob (smooth, controls);

    auto outputGroup = controls.removeFromRight (knobWidth * 3);
    placeKnob (input, outputGroup);
    placeKnob (mix, outputGroup);
    placeKnob (output, outputGroup);

    // The module's depth sits centred between the two groups.
    auto depthRow = controls.withSizeKeepingCentre (knobWidth, controls.getHeight());
    placeKnob (depth, depthRow);

    // Module tabs and curve tools.
    auto main = bounds.reduced (margin, 10);
    auto tabRow = main.removeFromTop (tabRowHeight - 10);

    for (auto& t : tabs)
    {
        auto tabArea = tabRow.removeFromLeft (112);
        t.power.setBounds (tabArea.removeFromLeft (24).withSizeKeepingCentre (20, 20));
        t.tab.setBounds (tabArea);
        tabRow.removeFromLeft (8);
    }

    gridBox.setBounds (tabRow.removeFromRight (100));

    for (auto* button : { &clearButton, &reverseButton, &invertButton, &shapesButton })
    {
        tabRow.removeFromRight (6);
        button->setBounds (tabRow.removeFromRight (72));
    }

    main.removeFromTop (10);
    curveEditor.setBounds (main);
}

//==============================================================================
void ContourEditor::timerCallback()
{
    curveEditor.setPlayhead (processor.displayPhase.load (std::memory_order_relaxed));

    const auto synced = processor.apvts.getRawParameterValue (Params::sync)->load() > 0.5f;
    rate.slider.setVisible (synced);
    rateHz.slider.setVisible (! synced);
    rate.label.setVisible (synced);
    rateHz.label.setVisible (! synced);

    const auto moduleOn = processor.apvts.getRawParameterValue (Params::onId (currentModule))->load() > 0.5f;
    curveEditor.setActive (moduleOn);

    refreshPresetLabel();
}

void ContourEditor::stateReloaded()
{
    selectModule (currentModule);
    refreshPresetLabel();
}
