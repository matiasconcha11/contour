#include "PresetManager.h"

namespace
{
    // Applies one factory preset on top of the default state.
    struct PresetBuilder
    {
        ContourProcessor& processor;

        PresetBuilder& set (const juce::String& id, float plainValue)
        {
            auto* param = processor.apvts.getParameter (id);
            param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
            return *this;
        }

        PresetBuilder& rate (const juce::String& name)
        {
            return set (Params::rate, (float) Params::rateNames().indexOf (name));
        }

        PresetBuilder& module (int m, bool on, float depth, std::vector<CurvePoint> shape)
        {
            set (Params::onId (m), on ? 1.0f : 0.0f);
            set (Params::depthId (m), depth);
            processor.curves[(size_t) m].setPoints (std::move (shape));
            return *this;
        }
    };

    struct FactoryPreset
    {
        const char* category;
        const char* name;
        std::function<void (PresetBuilder&)> build;
    };

    const std::vector<FactoryPreset>& factoryPresets()
    {
        using P = Params::Module;
        const auto off = [] (PresetBuilder& b, int m) { b.set (Params::onId (m), 0.0f); };

        static const std::vector<FactoryPreset> list {
            { "", "Init", [] (PresetBuilder&) {} },

            { "Volume", "Sidechain 1/4", [] (PresetBuilder& b) { b.rate ("1/4").module (P::volume, true, 1.0f, Curve::preset (Curve::duck)); } },
            { "Volume", "Soft Pump 1/4", [] (PresetBuilder& b) {
                  b.rate ("1/4").set (Params::smooth, 4.0f).module (P::volume, true, 0.8f, Curve::preset (Curve::pump));
              } },
            { "Volume", "Half-Time Pump", [] (PresetBuilder& b) { b.rate ("1/2").module (P::volume, true, 1.0f, Curve::preset (Curve::duck)); } },
            { "Volume", "Trance Gate", [] (PresetBuilder& b) {
                  b.rate ("1 bar").set (Params::smooth, 2.0f).module (P::volume, true, 1.0f, Curve::preset (Curve::tranceGate));
              } },
            { "Volume", "Chopper 1/16", [] (PresetBuilder& b) {
                  b.rate ("1/8").set (Params::smooth, 1.0f).module (P::volume, true, 1.0f, Curve::gate ("x."));
              } },
            { "Volume", "Triplet Chop", [] (PresetBuilder& b) {
                  b.rate ("1/8T").set (Params::smooth, 1.0f).module (P::volume, true, 0.9f, Curve::gate ("x."));
              } },
            { "Volume", "Swell 1 Bar", [] (PresetBuilder& b) { b.rate ("1 bar").module (P::volume, true, 1.0f, Curve::preset (Curve::rampUp)); } },
            { "Volume", "Self Duck (Audio)", [] (PresetBuilder& b) {
                  b.set (Params::mode, (float) Params::audioMode).set (Params::threshold, -20.0f).rate ("1/4")
                      .module (P::volume, true, 1.0f, Curve::preset (Curve::duck));
              } },
            { "Volume", "Kick Duck (Sidechain)", [] (PresetBuilder& b) {
                  b.set (Params::mode, (float) Params::audioMode).set (Params::triggerSource, 1.0f).set (Params::threshold, -24.0f)
                      .rate ("1/4").module (P::volume, true, 1.0f, Curve::preset (Curve::duck));
              } },

            { "Stereo", "Auto-Pan 1/2", [off] (PresetBuilder& b) {
                  off (b, P::volume);
                  b.rate ("1/2").module (P::pan, true, 0.8f, Curve::preset (Curve::sine));
              } },
            { "Stereo", "Ping-Pong 1/8", [off] (PresetBuilder& b) {
                  off (b, P::volume);
                  b.rate ("1/4").set (Params::smooth, 3.0f).module (P::pan, true, 0.7f, Curve::gate ("x."));
              } },
            { "Stereo", "Width Breathe", [off] (PresetBuilder& b) {
                  off (b, P::volume);
                  b.rate ("1 bar").module (P::width, true, 1.0f, Curve::preset (Curve::sine));
              } },
            { "Stereo", "Wide Pump", [] (PresetBuilder& b) {
                  b.rate ("1/4").module (P::volume, true, 0.9f, Curve::preset (Curve::duck))
                      .module (P::width, true, 0.6f, Curve::preset (Curve::rampUp));
              } },

            { "Filter", "Filter Sweep 1 Bar", [off] (PresetBuilder& b) {
                  off (b, P::volume);
                  b.rate ("1 bar").set (Params::filterResonance, 0.5f).module (P::filter, true, 0.85f, Curve::preset (Curve::sine));
              } },
            { "Filter", "Filter Pump 1/4", [off] (PresetBuilder& b) {
                  off (b, P::volume);
                  b.rate ("1/4").set (Params::filterResonance, 0.35f).module (P::filter, true, 0.8f, Curve::preset (Curve::duck));
              } },
            { "Filter", "High-pass Riser 4 Bars", [off] (PresetBuilder& b) {
                  off (b, P::volume);
                  b.rate ("4 bars").set (Params::filterType, 1.0f).set (Params::filterResonance, 0.4f)
                      .module (P::filter, true, 0.9f, Curve::preset (Curve::rampUp));
              } },
            { "Filter", "Wah 1/8", [off] (PresetBuilder& b) {
                  off (b, P::volume);
                  b.rate ("1/8").set (Params::filterType, 2.0f).set (Params::filterResonance, 0.6f)
                      .module (P::filter, true, 0.7f, Curve::preset (Curve::triangle));
              } },

            { "Lo-Fi", "Drive Pulse 1/4", [off] (PresetBuilder& b) {
                  off (b, P::volume);
                  b.rate ("1/4").module (P::drive, true, 0.7f, Curve::preset (Curve::rampDown));
              } },
            { "Lo-Fi", "Crush Gate 1/8", [off] (PresetBuilder& b) {
                  off (b, P::volume);
                  b.rate ("1/4").module (P::crush, true, 0.8f, Curve::gate (".x"));
              } },
            { "Lo-Fi", "Dusty Pump", [] (PresetBuilder& b) {
                  b.rate ("1/4").module (P::volume, true, 0.7f, Curve::preset (Curve::pump))
                      .module (P::crush, true, 0.45f, Curve::preset (Curve::rampDown))
                      .module (P::filter, true, 0.4f, Curve::preset (Curve::rampUp));
              } },

            { "Time", "Tape Stop 1 Bar", [off] (PresetBuilder& b) {
                  off (b, P::volume);
                  b.rate ("1 bar").set (Params::smooth, 5.0f).module (P::time, true, 1.0f, Curve::preset (Curve::tapeStop));
              } },
            { "Time", "Stutter 1/4", [off] (PresetBuilder& b) {
                  off (b, P::volume);
                  b.rate ("1/4").set (Params::smooth, 0.5f).module (P::time, true, 1.0f, Curve::preset (Curve::stairs));
              } },
            { "Time", "Reverse 1/2", [off] (PresetBuilder& b) {
                  off (b, P::volume);
                  b.rate ("1/2").set (Params::smooth, 0.5f)
                      .module (P::time, true, 1.0f, { { 0.0f, 1.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, { 0.999f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
              } },
        };

        return list;
    }
}

PresetManager::PresetManager (ContourProcessor& p, juce::File directory)
    : processor (p), userDirectory (std::move (directory))
{
    refresh();
}

juce::File PresetManager::defaultUserDirectory()
{
    const auto presetsRoot = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Presets");
    const auto standard = presetsRoot.getChildFile ("Rhizr/Contour");

    // Presets saved before the Rhizr rename lived under the developer's own name: move them over once.
    if (const auto legacy = presetsRoot.getChildFile ("Matias Concha/Contour"); legacy.isDirectory() && ! standard.exists())
        if (standard.getParentDirectory().createDirectory().wasOk())
            legacy.moveFileTo (standard);

    // ~/Library/Audio/Presets may point at an external drive that isn't mounted.
    if (standard.isDirectory() || standard.createDirectory().wasOk())
        return standard;

    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Contour Presets");
}

int PresetManager::numFactoryPresets()
{
    return (int) factoryPresets().size();
}

void PresetManager::refresh()
{
    presets.clear();

    for (const auto& factory : factoryPresets())
        presets.push_back ({ factory.name, true, {}, factory.category });

    auto files = userDirectory.findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension);
    std::sort (files.begin(), files.end(), [] (const auto& a, const auto& b)
               { return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0; });

    for (const auto& file : files)
        presets.push_back ({ file.getFileNameWithoutExtension(), false, file, "User" });
}

void PresetManager::applyFactory (int index)
{
    const ContourProcessor::ScopedPresetLoad loading (processor);

    for (auto* param : processor.getParameters())
        if (param != processor.getBypassParameter())
            param->setValueNotifyingHost (param->getDefaultValue());

    for (int m = 0; m < Params::numModules; ++m)
        processor.curves[(size_t) m].setPoints (Params::defaultCurve (m));

    PresetBuilder builder { processor };
    factoryPresets()[(size_t) index].build (builder);
    processor.presetName = factoryPresets()[(size_t) index].name;
}

bool PresetManager::load (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) presets.size()))
        return false;

    const auto& preset = presets[(size_t) index];

    if (preset.factory)
    {
        applyFactory (index);
    }
    else
    {
        const auto xml = juce::XmlDocument::parse (preset.file);

        if (xml == nullptr)
            return false;

        auto state = juce::ValueTree::fromXml (*xml);

        if (! state.hasType (processor.apvts.state.getType()))
            return false;

        // Presets never change whether the plugin is bypassed.
        for (auto child : state)
            if (child.getProperty ("id").toString() == Params::bypass)
                child.setProperty ("value", processor.apvts.getRawParameterValue (Params::bypass)->load(), nullptr);

        state.setProperty ("presetName", preset.name, nullptr);
        state.setProperty ("presetDirty", false, nullptr);
        processor.setStateTree (state);
    }

    processor.undoManager.clearUndoHistory();
    processor.updateHostDisplay (juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged (true));
    return true;
}

void PresetManager::loadInit()
{
    load (0);
}

void PresetManager::step (int delta)
{
    const auto count = (int) presets.size();
    const auto current = getCurrentIndex();
    const auto next = current < 0 ? 0 : ((current + delta) % count + count) % count;
    load (next);
}

juce::String PresetManager::sanitiseName (const juce::String& name)
{
    return juce::File::createLegalFileName (name.trim()).substring (0, 64);
}

bool PresetManager::save (const juce::String& requestedName)
{
    const auto name = sanitiseName (requestedName);

    if (name.isEmpty() || ! (userDirectory.isDirectory() || userDirectory.createDirectory().wasOk()))
        return false;

    processor.presetName = name;
    auto state = processor.getStateTree();
    state.setProperty ("presetDirty", false, nullptr);

    const auto xml = state.createXml();
    const auto file = userDirectory.getChildFile (name + fileExtension);

    if (xml == nullptr || ! xml->writeTo (file))
        return false;

    processor.markPresetClean();
    refresh();
    return true;
}

bool PresetManager::remove (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) presets.size()) || presets[(size_t) index].factory)
        return false;

    const auto wasCurrent = index == getCurrentIndex();

    if (! presets[(size_t) index].file.deleteFile())
        return false;

    refresh();

    // The deleted preset's sound stays loaded, but now as unsaved changes.
    if (wasCurrent)
        processor.curveChanged();

    return true;
}

int PresetManager::getCurrentIndex() const
{
    for (size_t i = presets.size(); i-- > 0;) // user presets win over a factory one with the same name
        if (presets[i].name == processor.presetName)
            return (int) i;

    return -1;
}

bool PresetManager::isCurrentUserPreset() const
{
    const auto index = getCurrentIndex();
    return index >= 0 && ! presets[(size_t) index].factory;
}
