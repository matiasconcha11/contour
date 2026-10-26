#include "PluginEditor.h"

namespace
{
    constexpr int headerHeight = 56;
    constexpr int controlsHeight = 162;
    constexpr int sidebarWidth = 184;
    constexpr int toolRowHeight = 30;
    constexpr int margin = 16;
    constexpr int captionHeight = 18;

    // Curve edit that can be undone; the first perform() re-applies what the gesture already did.
    class CurveEdit : public juce::UndoableAction
    {
    public:
        CurveEdit (ContourProcessor& p, int m, std::vector<CurvePoint> b, std::vector<CurvePoint> a)
            : processor (p), module (m), before (std::move (b)), after (std::move (a)) {}

        bool perform() override { return apply (after); }
        bool undo() override { return apply (before); }

    private:
        bool apply (const std::vector<CurvePoint>& points)
        {
            processor.curves[(size_t) module].setPoints (points);
            processor.curveChanged();
            return true;
        }

        ContourProcessor& processor;
        int module;
        std::vector<CurvePoint> before, after;
    };

    bool samePoints (const std::vector<CurvePoint>& a, const std::vector<CurvePoint>& b)
    {
        return std::equal (a.begin(), a.end(), b.begin(), b.end(), [] (const auto& p, const auto& q)
                           { return p.x == q.x && p.y == q.y && p.tension == q.tension; });
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
      moduleStrip (p),
      curveEditor (p.curves[Params::volume], p.waveIn, p.waveOut),
      modeSelector (*p.apvts.getParameter (Params::mode), Params::modeNames())
{
    setLookAndFeel (&lookAndFeel);
    setWantsKeyboardFocus (true);

    // Header: presets and bypass.
    for (auto* button : { &prevButton, &nextButton, &saveButton, &presetButton })
        addAndMakeVisible (button);

    prevButton.setTooltip ("Previous preset");
    nextButton.setTooltip ("Next preset");
    prevButton.onClick = [this] { presets.step (-1); stateReloaded(); };
    nextButton.onClick = [this] { presets.step (1); stateReloaded(); };
    presetButton.onClick = [this] { showPresetMenu(); };
    saveButton.onClick = [this] { savePreset(); };

    bypassButton.setClickingTogglesState (true);
    bypassButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffff6b6b));
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, Params::bypass, bypassButton);
    addAndMakeVisible (bypassButton);

    // Module list.
    moduleStrip.onSelect = [this] (int m) { selectModule (m); };
    addAndMakeVisible (moduleStrip);

    // Curve tools.
    for (auto* button : { &shapesButton, &invertButton, &reverseButton, &copyButton, &pasteButton, &clearButton, &undoButton, &redoButton })
        addAndMakeVisible (button);

    shapesButton.onClick = [this] { showShapesMenu(); };
    invertButton.onClick = [this] { editCurve ([] (Curve& c) { c.invert(); }); };
    reverseButton.onClick = [this] { editCurve ([] (Curve& c) { c.reverse(); }); };
    clearButton.onClick = [this] { editCurve ([this] (Curve& c) { c.setPoints (Curve::preset (Params::neutralShape (currentModule))); }); };
    copyButton.onClick = [this] { copyCurve(); };
    pasteButton.onClick = [this] { pasteCurve(); };
    undoButton.onClick = [this] { processor.undoManager.undo(); };
    redoButton.onClick = [this] { processor.undoManager.redo(); };
    copyButton.setTooltip ("Copy this curve (paste it into any module or instance)");
    undoButton.setTooltip ("Undo curve edit (Cmd+Z)");
    redoButton.setTooltip ("Redo curve edit (Cmd+Shift+Z)");

    gridBox.addItemList ({ "Grid 1/4", "Grid 1/8", "Grid 1/16", "Grid 1/32", "Grid Off" }, 1);
    gridBox.onChange = [this]
    {
        static constexpr int divisions[] { 4, 8, 16, 32, 1 };
        curveEditor.setGridDivisions (divisions[juce::jlimit (0, 4, gridBox.getSelectedItemIndex())]);
    };
    gridBox.setSelectedItemIndex (2);
    addAndMakeVisible (gridBox);

    curveEditor.onEditStart = [this] { beginCurveEdit(); };
    curveEditor.onEditEnd = [this] { endCurveEdit(); };
    addAndMakeVisible (curveEditor);

    // Timing.
    setUpKnob (rate, Params::rate, "Rate");
    setUpKnob (rateHz, Params::rateHz, "Rate");
    setUpKnob (offset, Params::offset, "Offset");
    setUpKnob (smooth, Params::smooth, "Smooth");
    setUpKnob (threshold, Params::threshold, "Threshold");
    modeSelector.setTooltip ("Tempo follows the host; Hz runs freely; Audio restarts the curve on each hit above the threshold");
    addAndMakeVisible (modeSelector);

    // Module.
    setUpKnob (depth, Params::depthId (Params::volume), "Depth");
    setUpKnob (resonance, Params::filterResonance, "Resonance");
    filterTypeBox.addItemList (Params::filterTypeNames(), 1);
    filterTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, Params::filterType, filterTypeBox);
    addAndMakeVisible (filterTypeBox);

    // Output.
    setUpKnob (input, Params::inputGain, "Input", true);
    setUpKnob (mix, Params::mix, "Mix");
    setUpKnob (output, Params::outputGain, "Output", true);
    addAndMakeVisible (meter);

    addChildComponent (dialog);

    stateReloaded();

    setResizable (true, true);
    setResizeLimits (920, 620, 1900, 1300);
    setSize (1060, 700);

    timerCallback(); // set up visibility before the first paint
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
    moduleStrip.setSelected (module);

    const auto accent = Theme::modules[module];
    curveEditor.setCurve (processor.curves[(size_t) module], accent, Params::isBipolar (module));
    curveEditor.setCaption (Params::moduleNames[module].toUpperCase(), Params::moduleHint (module));

    depth.attachment.reset();
    depth.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, Params::depthId (module), depth.slider);
    depth.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    resonance.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);

    const auto isFilter = module == Params::filter;
    resonance.slider.setVisible (isFilter);
    resonance.label.setVisible (isFilter);
    filterTypeBox.setVisible (isFilter);

    for (auto* button : { &shapesButton, &invertButton, &reverseButton, &copyButton, &pasteButton, &clearButton })
        button->setColour (juce::TextButton::buttonOnColourId, accent);

    resized();
    repaint();
}

//==============================================================================
void ContourEditor::beginCurveEdit()
{
    pointsBeforeEdit = processor.curves[(size_t) currentModule].getPoints();
}

void ContourEditor::endCurveEdit()
{
    const auto& after = processor.curves[(size_t) currentModule].getPoints();

    if (samePoints (pointsBeforeEdit, after))
        return;

    processor.undoManager.beginNewTransaction();
    processor.undoManager.perform (new CurveEdit (processor, currentModule, pointsBeforeEdit, after));
    pointsBeforeEdit = after;
}

void ContourEditor::editCurve (const std::function<void (Curve&)>& change)
{
    beginCurveEdit();
    change (processor.curves[(size_t) currentModule]);
    endCurveEdit();
    curveEditor.repaint();
}

void ContourEditor::copyCurve()
{
    juce::SystemClipboard::copyTextToClipboard (processor.curves[(size_t) currentModule].toValueTree().toXmlString());
}

void ContourEditor::pasteCurve()
{
    const auto xml = juce::parseXML (juce::SystemClipboard::getTextFromClipboard());

    if (xml == nullptr)
        return;

    const auto tree = juce::ValueTree::fromXml (*xml);

    if (tree.hasType ("CURVE") && tree.getNumChildren() >= 2)
        editCurve ([&tree] (Curve& c) { c.fromValueTree (tree); });
}

bool ContourEditor::keyPressed (const juce::KeyPress& key)
{
    // Hosts often keep Cmd+Z for themselves; the Undo/Redo buttons always work.
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))
        return processor.undoManager.undo();

    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
        return processor.undoManager.redo();

    return false;
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

    const auto& list = presets.getPresets();
    const auto current = presets.getCurrentIndex();

    // Factory presets grouped by category, user presets in their own section.
    juce::PopupMenu menu;
    juce::StringArray categories;

    for (const auto& preset : list)
        if (preset.factory && preset.category.isNotEmpty())
            categories.addIfNotAlreadyThere (preset.category);

    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i].factory && list[(size_t) i].category.isEmpty())
            menu.addItem (i + 1, list[(size_t) i].name, true, i == current);

    for (const auto& category : categories)
    {
        juce::PopupMenu sub;
        auto containsCurrent = false;

        for (int i = 0; i < (int) list.size(); ++i)
        {
            if (list[(size_t) i].factory && list[(size_t) i].category == category)
            {
                sub.addItem (i + 1, list[(size_t) i].name, true, i == current);
                containsCurrent |= i == current;
            }
        }

        menu.addSubMenu (category, sub, true, nullptr, containsCurrent);
    }

    menu.addSectionHeader ("User");
    auto anyUser = false;

    for (int i = 0; i < (int) list.size(); ++i)
    {
        if (! list[(size_t) i].factory)
        {
            menu.addItem (i + 1, list[(size_t) i].name, true, i == current);
            anyUser = true;
        }
    }

    if (! anyUser)
        menu.addItem (-1, "No saved presets yet", false, false);

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
        if (result > 0)
            editCurve ([result] (Curve& c) { c.setPoints (Curve::preset (result - 1)); });
    });
}

//==============================================================================
void ContourEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    // Header bar with logo mark.
    const auto header = getLocalBounds().withHeight (headerHeight);
    g.setColour (Theme::panel);
    g.fillRect (header);
    g.setColour (Theme::outline);
    g.drawHorizontalLine (headerHeight - 1, 0.0f, (float) getWidth());

    const auto mark = juce::Rectangle<float> ((float) margin, 16.0f, 26.0f, 24.0f);
    juce::Path logo;
    logo.startNewSubPath (mark.getX(), mark.getBottom());
    logo.cubicTo (mark.getX() + 6.0f, mark.getY() - 4.0f, mark.getX() + 10.0f, mark.getY(), mark.getCentreX(), mark.getCentreY());
    logo.cubicTo (mark.getX() + 16.0f, mark.getBottom(), mark.getRight() - 4.0f, mark.getBottom(), mark.getRight(), mark.getY());
    g.setGradientFill (juce::ColourGradient (Theme::modules[0], mark.getTopLeft(), Theme::modules[6], mark.getBottomRight(), false));
    g.strokePath (logo, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    auto logoText = header.withTrimmedLeft (margin + 36);
    g.setColour (Theme::text);
    g.setFont (Theme::caps (18.0f).withExtraKerningFactor (0.3f));
    g.drawText ("CONTOUR", logoText, juce::Justification::centredLeft);
    g.setColour (Theme::faint);
    g.setFont (Theme::font (11.0f));
    g.drawText ("v" CONTOUR_VERSION, logoText.withTrimmedLeft (128), juce::Justification::centredLeft);

    // Controls panel and group captions.
    const auto controls = getLocalBounds().withTop (getHeight() - controlsHeight);
    g.setColour (Theme::panel);
    g.fillRect (controls);
    g.setColour (Theme::outline);
    g.drawHorizontalLine (controls.getY(), 0.0f, (float) getWidth());

    const auto accent = Theme::modules[currentModule];
    const std::tuple<juce::Rectangle<int>, juce::String, juce::Colour> groups[] {
        { timingGroup, "TIMING", Theme::dim },
        { moduleGroup, Params::moduleNames[currentModule].toUpperCase(), accent },
        { outputGroup, "OUTPUT", Theme::dim },
    };

    for (const auto& [area, caption, colour] : groups)
    {
        const auto line = area.withHeight (captionHeight);
        g.setFont (Theme::caps (10.0f));
        g.setColour (colour);
        g.drawText (caption, line, juce::Justification::centredLeft);
        g.setColour (Theme::outline);
        const auto textWidth = juce::GlyphArrangement::getStringWidthInt (Theme::caps (10.0f), caption) + 8;
        g.drawHorizontalLine (line.getCentreY(), (float) (line.getX() + textWidth), (float) line.getRight());
    }

    // Trigger indicator next to the mode switch.
    if (triggerFlash > 0.01f)
    {
        const auto dot = juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ (float) modeSelector.getRight() + 9.0f, (float) modeSelector.getBounds().getCentreY() });
        g.setColour (Theme::modules[0].withAlpha (triggerFlash));
        g.fillEllipse (dot);
    }
}

void ContourEditor::resized()
{
    auto bounds = getLocalBounds();
    dialog.setBounds (bounds);

    // Header: logo on the left, preset bar centred, bypass on the right.
    auto header = bounds.removeFromTop (headerHeight).reduced (margin, 12);
    bypassButton.setBounds (header.removeFromRight (80));

    auto presetBar = header.withSizeKeepingCentre (juce::jmin (440, header.getWidth() - 360), header.getHeight());
    saveButton.setBounds (presetBar.removeFromRight (64));
    presetBar.removeFromRight (6);
    nextButton.setBounds (presetBar.removeFromRight (32));
    presetBar.removeFromRight (4);
    prevButton.setBounds (presetBar.removeFromLeft (32));
    presetBar.removeFromLeft (4);
    presetButton.setBounds (presetBar);

    // Controls: TIMING | MODULE | OUTPUT.
    auto controls = bounds.removeFromBottom (controlsHeight).reduced (margin, 10);
    const auto knobWidth = juce::jmin (88, (controls.getWidth() - 80) / 10);

    const auto placeKnob = [knobWidth] (Knob& knob, juce::Rectangle<int>& row)
    {
        auto area = row.removeFromLeft (knobWidth);
        knob.label.setBounds (area.removeFromTop (16));
        area.removeFromBottom (26);
        knob.slider.setBounds (area);
        return area;
    };

    timingGroup = controls.removeFromLeft (knobWidth * 4);
    outputGroup = controls.removeFromRight (knobWidth * 3 + 22);
    moduleGroup = controls.withSizeKeepingCentre (knobWidth * 2, controls.getHeight());

    {
        auto row = timingGroup.withTrimmedTop (captionHeight + 2);
        const auto strip = row.withTop (row.getBottom() - 24);
        const auto rateArea = placeKnob (rate, row);
        rateHz.label.setBounds (rate.label.getBounds());
        rateHz.slider.setBounds (rateArea);
        placeKnob (offset, row);
        placeKnob (smooth, row);
        placeKnob (threshold, row);
        modeSelector.setBounds (strip.withWidth (knobWidth * 3).reduced (6, 2));
    }

    {
        auto row = moduleGroup.withTrimmedTop (captionHeight + 2);
        const auto strip = row.withTop (row.getBottom() - 24);
        placeKnob (depth, row);
        placeKnob (resonance, row);
        filterTypeBox.setBounds (strip.withTrimmedLeft (knobWidth).reduced (4, 2));

        // Without the filter's extra controls, centre the depth knob.
        if (currentModule != Params::filter)
        {
            const auto shift = knobWidth / 2;
            depth.slider.setBounds (depth.slider.getBounds().translated (shift, 0));
            depth.label.setBounds (depth.label.getBounds().translated (shift, 0));
        }
    }

    {
        auto row = outputGroup.withTrimmedTop (captionHeight + 2);
        meter.setBounds (row.removeFromRight (12).withTrimmedTop (16).withTrimmedBottom (26));
        row.removeFromRight (10);
        placeKnob (input, row);
        placeKnob (mix, row);
        placeKnob (output, row);
    }

    // Body: module list on the left, tools and curve on the right.
    auto body = bounds.reduced (margin, 12);
    moduleStrip.setBounds (body.removeFromLeft (sidebarWidth));
    body.removeFromLeft (14);

    auto tools = body.removeFromTop (toolRowHeight);
    gridBox.setBounds (tools.removeFromRight (104));
    tools.removeFromRight (12);

    for (auto* button : { &redoButton, &undoButton })
    {
        button->setBounds (tools.removeFromRight (56));
        tools.removeFromRight (4);
    }

    tools.removeFromRight (8);

    const auto toolWidth = juce::jmin (76, (tools.getWidth() - 25) / 6);

    for (auto* button : { &shapesButton, &invertButton, &reverseButton, &copyButton, &pasteButton, &clearButton })
    {
        button->setBounds (tools.removeFromLeft (toolWidth));
        tools.removeFromLeft (5);
    }

    body.removeFromTop (10);
    curveEditor.setBounds (body);
}

//==============================================================================
void ContourEditor::timerCallback()
{
    curveEditor.setPlayhead (processor.displayPhase.load (std::memory_order_relaxed));

    const auto mode = (int) processor.apvts.getRawParameterValue (Params::mode)->load();
    const auto hz = mode == Params::hzMode;
    rate.slider.setVisible (! hz);
    rate.label.setVisible (! hz);
    rateHz.slider.setVisible (hz);
    rateHz.label.setVisible (hz);
    threshold.slider.setEnabled (mode == Params::audioMode);
    threshold.label.setAlpha (mode == Params::audioMode ? 1.0f : 0.4f);

    const auto moduleOn = processor.apvts.getRawParameterValue (Params::onId (currentModule))->load() > 0.5f;
    curveEditor.setActive (moduleOn);

    meter.setLevels (processor.outputPeakLeft.exchange (0.0f), processor.outputPeakRight.exchange (0.0f));

    const auto triggers = processor.triggerCount.load();
    triggerFlash = triggers != lastTriggerCount ? 1.0f : triggerFlash * 0.85f;
    lastTriggerCount = triggers;

    if (mode == Params::audioMode)
        repaint (modeSelector.getBounds().withWidth (modeSelector.getWidth() + 20));

    undoButton.setEnabled (processor.undoManager.canUndo());
    redoButton.setEnabled (processor.undoManager.canRedo());

    // Thumbnails change with edits, presets and undo; a few repaints a second is plenty.
    if (++frameCount % 6 == 0)
        moduleStrip.repaint();

    refreshPresetLabel();
}

void ContourEditor::stateReloaded()
{
    // Show a module that is actually doing something, if the current one is off.
    const auto isOn = [this] (int m) { return processor.apvts.getRawParameterValue (Params::onId (m))->load() > 0.5f; };
    auto module = currentModule;

    if (! isOn (module))
        for (int m = 0; m < Params::numModules; ++m)
            if (isOn (m))
            {
                module = m;
                break;
            }

    selectModule (module);
    refreshPresetLabel();
}
