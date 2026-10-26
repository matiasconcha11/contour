#pragma once

#include "PluginProcessor.h"

// Factory presets live in code; user presets are XML files in the user preset folder.
// Message thread only.
class PresetManager
{
public:
    static constexpr const char* fileExtension = ".contourpreset";

    struct Preset
    {
        juce::String name;
        bool factory = false;
        juce::File file;
    };

    explicit PresetManager (ContourProcessor&, juce::File userDirectory = defaultUserDirectory());

    // Factory presets first, then user presets sorted by name.
    const std::vector<Preset>& getPresets() const { return presets; }
    void refresh();

    bool load (int index);
    void loadInit();
    void step (int delta); // previous / next, wrapping around

    // Writes the current state as a user preset; an existing preset with the same name is replaced.
    bool save (const juce::String& name);
    bool remove (int index);

    int getCurrentIndex() const;
    bool isCurrentUserPreset() const;

    juce::File getUserDirectory() const { return userDirectory; }
    static juce::File defaultUserDirectory();
    static int numFactoryPresets();

private:
    static juce::String sanitiseName (const juce::String&);
    void applyFactory (int index);

    ContourProcessor& processor;
    juce::File userDirectory;
    std::vector<Preset> presets;
};
