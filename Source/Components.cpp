#include "Components.h"
#include "LookAndFeel.h"

namespace
{
    constexpr int captionHeight = 28;
    constexpr int rowHeight = 46;
    constexpr int rowGap = 4;
}

//==============================================================================
ModuleStrip::ModuleStrip (ContourProcessor& p) : processor (p)
{
    for (int m = 0; m < Params::numModules; ++m)
    {
        auto& power = powers[(size_t) m];
        power.setColour (juce::ToggleButton::tickColourId, Theme::modules[m]);
        power.setTooltip ("Turn " + Params::moduleNames[m] + " on or off");
        powerAttachments[(size_t) m] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            processor.apvts, Params::onId (m), power);
        addAndMakeVisible (power);
    }
}

void ModuleStrip::setSelected (int module)
{
    selected = module;
    repaint();
}

juce::Rectangle<int> ModuleStrip::rowBounds (int module) const
{
    return { 0, captionHeight + module * (rowHeight + rowGap), getWidth(), rowHeight };
}

int ModuleStrip::rowAt (juce::Point<int> position) const
{
    for (int m = 0; m < Params::numModules; ++m)
        if (rowBounds (m).contains (position))
            return m;

    return -1;
}

void ModuleStrip::paint (juce::Graphics& g)
{
    g.setFont (Theme::caps (10.5f));
    g.setColour (Theme::dim);
    g.drawText ("MODULES", getLocalBounds().removeFromTop (captionHeight - 8), juce::Justification::bottomLeft);

    for (int m = 0; m < Params::numModules; ++m)
    {
        const auto row = rowBounds (m).toFloat();
        const auto accent = Theme::modules[m];
        const auto on = powers[(size_t) m].getToggleState();
        const auto isSelected = m == selected;

        g.setColour (isSelected ? Theme::raised : (m == hovered ? Theme::panel.brighter (0.04f) : Theme::panel));
        g.fillRoundedRectangle (row, 5.0f);

        if (isSelected)
        {
            g.setColour (accent.withAlpha (0.5f));
            g.drawRoundedRectangle (row.reduced (0.5f), 5.0f, 1.0f);
            g.setColour (accent);
            g.fillRoundedRectangle (row.withWidth (3.0f).reduced (0.0f, 10.0f), 1.5f);
        }

        // Name.
        auto text = row.toNearestInt().withTrimmedLeft (34);
        auto nameArea = text.removeFromLeft (64);
        g.setFont (Theme::caps (11.0f));
        g.setColour (on ? (isSelected ? Theme::text : Theme::text.withAlpha (0.8f)) : Theme::dim);
        g.drawText (Params::moduleNames[m].toUpperCase(), nameArea, juce::Justification::centredLeft);

        // Thumbnail of the curve.
        const auto thumb = text.reduced (6, 11).toFloat();
        const auto& curve = processor.curves[(size_t) m];
        juce::Path path;

        for (int i = 0; i <= 40; ++i)
        {
            const auto x = (float) i / 40.0f;
            const juce::Point<float> point { thumb.getX() + x * thumb.getWidth(),
                                             thumb.getBottom() - curve.evaluate (x) * thumb.getHeight() };

            if (i == 0)
                path.startNewSubPath (point);
            else
                path.lineTo (point);
        }

        g.setColour ((on ? accent : Theme::faint).withAlpha (on ? 0.9f : 0.6f));
        g.strokePath (path, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void ModuleStrip::resized()
{
    for (int m = 0; m < Params::numModules; ++m)
        powers[(size_t) m].setBounds (rowBounds (m).withTrimmedLeft (8).removeFromLeft (22).withSizeKeepingCentre (20, 20));
}

void ModuleStrip::mouseDown (const juce::MouseEvent& e)
{
    if (const auto row = rowAt (e.getPosition()); row >= 0 && onSelect)
        onSelect (row);
}

void ModuleStrip::mouseMove (const juce::MouseEvent& e)
{
    if (const auto row = rowAt (e.getPosition()); row != hovered)
    {
        hovered = row;
        repaint();
    }
}

void ModuleStrip::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

//==============================================================================
void LevelMeter::setLevels (float left, float right)
{
    const float incoming[] { left, right };
    auto changed = false;

    for (int ch = 0; ch < 2; ++ch)
    {
        const auto next = juce::jmax (incoming[ch], levels[ch] * 0.86f);
        changed |= std::abs (next - levels[ch]) > 1.0e-4f;
        levels[ch] = next;
    }

    if (changed)
        repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto barWidth = (bounds.getWidth() - 2.0f) / 2.0f;

    for (int ch = 0; ch < 2; ++ch)
    {
        const auto bar = bounds.withWidth (barWidth).withX (bounds.getX() + (float) ch * (barWidth + 2.0f));
        g.setColour (Theme::background);
        g.fillRoundedRectangle (bar, 2.0f);

        // -60 dB to +6 dB.
        const auto db = juce::Decibels::gainToDecibels (levels[ch], -60.0f);
        const auto proportion = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
        const auto filled = bar.withTop (bar.getBottom() - proportion * bar.getHeight());

        g.setColour (db > 0.0f ? juce::Colour (0xffff6b6b) : (db > -6.0f ? juce::Colour (0xffffd166) : Theme::text.withAlpha (0.75f)));
        g.fillRoundedRectangle (filled, 2.0f);
    }

    // 0 dB tick.
    const auto zeroY = bounds.getBottom() - (60.0f / 66.0f) * bounds.getHeight();
    g.setColour (Theme::dim);
    g.drawHorizontalLine (juce::roundToInt (zeroY), bounds.getX(), bounds.getRight());
}

//==============================================================================
ChoiceSelector::ChoiceSelector (juce::RangedAudioParameter& parameter, const juce::StringArray& labels)
    : attachment (parameter, [this] (float value)
                  {
                      for (int i = 0; i < buttons.size(); ++i)
                          buttons[i]->setToggleState (i == juce::roundToInt (value), juce::dontSendNotification);
                  })
{
    for (int i = 0; i < labels.size(); ++i)
    {
        auto* button = buttons.add (new juce::TextButton (labels[i]));
        button->setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0)
                                   | (i < labels.size() - 1 ? juce::Button::ConnectedOnRight : 0));
        button->onClick = [this, i] { attachment.setValueAsCompleteGesture ((float) i); };
        addAndMakeVisible (button);
    }

    attachment.sendInitialUpdate();
}

void ChoiceSelector::setTooltip (const juce::String& text)
{
    for (auto* button : buttons)
        button->setTooltip (text);
}

void ChoiceSelector::resized()
{
    auto bounds = getLocalBounds();
    const auto width = bounds.getWidth() / juce::jmax (1, buttons.size());

    for (auto* button : buttons)
        button->setBounds (bounds.removeFromLeft (width));
}
