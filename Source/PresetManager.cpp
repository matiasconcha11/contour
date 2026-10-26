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
        const char* name;
        std::function<void (PresetBuilder&)> build;
    };

    const std::vector<FactoryPreset>& factoryPresets()
    {
        using P = Params::Module;

        static const std::vector<FactoryPreset> list {
            { "Init", [] (PresetBuilder&) {} },
            { "Sidechain 1/4", [] (PresetBuilder& b) { b.rate ("1/4").module (P::volume, true, 1.0f, Curve::preset (Curve::duck)); } },
            { "Soft Pump 1/4", [] (PresetBuilder& b) {
                  b.rate ("1/4").set (Params::smooth, 4.0f).module (P::volume, true, 0.8f, Curve::preset (Curve::pump));
              } },
            { "Half-Time Pump", [] (PresetBuilder& b) { b.rate ("1/2").module (P::volume, true, 1.0f, Curve::preset (Curve::duck)); } },
            { "Trance Gate", [] (PresetBuilder& b) {
                  b.rate ("1 bar").set (Params::smooth, 2.0f).module (P::volume, true, 1.0f, Curve::preset (Curve::tranceGate));
              } },
            { "Chopper 1/16", [] (PresetBuilder& b) {
                  b.rate ("1/8").set (Params::smooth, 1.0f).module (P::volume, true, 1.0f, Curve::gate ("x."));
              } },
            { "Triplet Chop", [] (PresetBuilder& b) {
                  b.rate ("1/8T").set (Params::smooth, 1.0f).module (P::volume, true, 0.9f, Curve::gate ("x."));
              } },
            { "Swell 1 Bar", [] (PresetBuilder& b) { b.rate ("1 bar").module (P::volume, true, 1.0f, Curve::preset (Curve::rampUp)); } },
            { "Tape Fade 2 Bars", [] (PresetBuilder& b) {
                  b.rate ("2 bars").module (P::volume, true, 1.0f, Curve::preset (Curve::rampDown));
              } },
            { "Auto-Pan 1/2", [] (PresetBuilder& b) {
                  b.rate ("1/2").module (P::volume, false, 1.0f, Curve::preset (Curve::flatTop))
                      .module (P::pan, true, 0.8f, Curve::preset (Curve::sine));
              } },
            { "Ping-Pong 1/8", [] (PresetBuilder& b) {
                  b.rate ("1/4").set (Params::smooth, 3.0f).module (P::volume, false, 1.0f, Curve::preset (Curve::flatTop))
                      .module (P::pan, true, 0.7f, Curve::gate ("x."));
              } },
            { "Width Breathe", [] (PresetBuilder& b) {
                  b.rate ("1 bar").module (P::volume, false, 1.0f, Curve::preset (Curve::flatTop))
                      .module (P::width, true, 1.0f, Curve::preset (Curve::sine));
              } },
            { "Wide Pump", [] (PresetBuilder& b) {
                  b.rate ("1/4").module (P::volume, true, 0.9f, Curve::preset (Curve::duck))
                      .module (P::width, true, 0.6f, Curve::preset (Curve::rampUp));
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
    const auto standard = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                              .getChildFile ("Library/Audio/Presets/Matias Concha/Contour");

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
        presets.push_back ({ factory.name, true, {} });

    auto files = userDirectory.findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension);
    std::sort (files.begin(), files.end(), [] (const auto& a, const auto& b)
               { return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0; });

    for (const auto& file : files)
        presets.push_back ({ file.getFileNameWithoutExtension(), false, file });
}

void PresetManager::applyFactory (int index)
{
    const ContourProcessor::ScopedPresetLoad loading (processor);

    for (auto* param : processor.getParameters())
        if (param != processor.getBypassParameter())
            param->setValueNotifyingHost (param->getDefaultValue());

    processor.curves[Params::volume].setPoints (Curve::preset (Curve::duck));
    processor.curves[Params::pan].setPoints (Curve::preset (Curve::sine));
    processor.curves[Params::width].setPoints (Curve::preset (Curve::sine));

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
