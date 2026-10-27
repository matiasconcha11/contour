#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <juce_audio_formats/juce_audio_formats.h>

#include "PresetManager.h"

using Catch::Matchers::WithinAbs;

namespace
{
    struct TempDirectory
    {
        TempDirectory() { file.createDirectory(); }
        ~TempDirectory() { file.deleteRecursively(); }
        juce::File getFile() const { return file; }

        juce::File file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("ContourPresetTests-" + juce::Uuid().toString());
    };

    struct Fixture
    {
        Fixture() : presets (processor, directory.getFile()) {}

        float param (const juce::String& id) { return processor.apvts.getRawParameterValue (id)->load(); }

        void setParam (const juce::String& id, float value)
        {
            auto* p = processor.apvts.getParameter (id);
            p->setValueNotifyingHost (p->convertTo0to1 (value));
        }

        juce::ScopedJuceInitialiser_GUI juce;
        TempDirectory directory;
        ContourProcessor processor;
        PresetManager presets;
    };
}

TEST_CASE_METHOD (Fixture, "Every factory preset loads cleanly", "[presets]")
{
    REQUIRE (PresetManager::numFactoryPresets() > 10);

    for (int i = 0; i < PresetManager::numFactoryPresets(); ++i)
    {
        REQUIRE (presets.load (i));
        CHECK (processor.presetName == presets.getPresets()[(size_t) i].name);
        CHECK_FALSE (processor.isPresetDirty());
        CHECK (presets.getCurrentIndex() == i);
    }
}

TEST_CASE_METHOD (Fixture, "Factory presets start from defaults", "[presets]")
{
    setParam (Params::outputGain, -12.0f);
    presets.load (presets.getPresets().size() > 1 ? 1 : 0);

    CHECK_THAT (param (Params::outputGain), WithinAbs (0.0f, 1.0e-4));
}

TEST_CASE_METHOD (Fixture, "Editing marks the preset dirty", "[presets]")
{
    presets.load (1);
    REQUIRE_FALSE (processor.isPresetDirty());

    setParam (Params::mix, 0.4f);
    CHECK (processor.isPresetDirty());

    presets.load (1);
    processor.curveChanged();
    CHECK (processor.isPresetDirty());
}

TEST_CASE_METHOD (Fixture, "Bypass is neither saved into nor changed by presets", "[presets]")
{
    presets.load (1);
    setParam (Params::bypass, 1.0f);

    CHECK_FALSE (processor.isPresetDirty());

    presets.load (2);
    CHECK (param (Params::bypass) > 0.5f);
}

TEST_CASE_METHOD (Fixture, "User presets save and load back exactly", "[presets]")
{
    const std::vector<CurvePoint> shape { { 0.0f, 0.1f, 0.0f }, { 0.3f, 0.7f, 0.5f }, { 1.0f, 0.1f, 0.0f } };
    processor.curves[Params::pan].setPoints (shape);
    setParam (Params::onId (Params::pan), 1.0f);
    setParam (Params::mix, 0.6f);

    REQUIRE (presets.save ("My Pan"));
    CHECK (directory.getFile().getChildFile ("My Pan.contourpreset").existsAsFile());
    CHECK (processor.presetName == "My Pan");
    CHECK_FALSE (processor.isPresetDirty());
    CHECK (presets.isCurrentUserPreset());

    presets.loadInit();
    REQUIRE (param (Params::onId (Params::pan)) < 0.5f);

    const auto index = presets.getCurrentIndex() >= 0 ? (int) presets.getPresets().size() - 1 : -1;
    REQUIRE (presets.load (index));

    CHECK (processor.presetName == "My Pan");
    CHECK (param (Params::onId (Params::pan)) > 0.5f);
    CHECK_THAT (param (Params::mix), WithinAbs (0.6f, 1.0e-4));
    CHECK_FALSE (processor.isPresetDirty());

    const auto& points = processor.curves[Params::pan].getPoints();
    REQUIRE (points.size() == shape.size());
    CHECK (points[1].y == shape[1].y);
    CHECK (points[1].tension == shape[1].tension);
}

TEST_CASE_METHOD (Fixture, "Saving with an existing name overwrites it", "[presets]")
{
    REQUIRE (presets.save ("Twice"));
    setParam (Params::mix, 0.25f);
    REQUIRE (presets.save ("Twice"));

    const auto userCount = (int) presets.getPresets().size() - PresetManager::numFactoryPresets();
    CHECK (userCount == 1);

    presets.loadInit();
    presets.load ((int) presets.getPresets().size() - 1);
    CHECK_THAT (param (Params::mix), WithinAbs (0.25f, 1.0e-4));
}

TEST_CASE_METHOD (Fixture, "Unsafe names are cleaned up", "[presets]")
{
    REQUIRE (presets.save ("  a/b:c  "));
    CHECK (presets.getPresets().back().file.getParentDirectory() == directory.getFile());
    CHECK_FALSE (presets.save ("   "));
}

TEST_CASE_METHOD (Fixture, "User presets can be deleted, factory ones cannot", "[presets]")
{
    REQUIRE (presets.save ("Temp"));
    const auto index = presets.getCurrentIndex();

    CHECK_FALSE (presets.remove (0));
    REQUIRE (presets.remove (index));
    CHECK_FALSE (directory.getFile().getChildFile ("Temp.contourpreset").exists());
    CHECK ((int) presets.getPresets().size() == PresetManager::numFactoryPresets());
    CHECK (processor.isPresetDirty()); // the sound is still loaded, just unsaved
}

TEST_CASE_METHOD (Fixture, "Next and previous wrap around the list", "[presets]")
{
    presets.load (0);
    presets.step (-1);
    CHECK (presets.getCurrentIndex() == (int) presets.getPresets().size() - 1);

    presets.step (1);
    CHECK (presets.getCurrentIndex() == 0);
}

// Not part of the normal run: renders the editor to a PNG for a visual check.
//   ./build/ContourTests "[.snapshot]"   (writes to $CONTOUR_SNAPSHOT or /tmp/contour.png)
#include "PluginEditor.h"

TEST_CASE ("Editor snapshot", "[.snapshot]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    ContourProcessor processor;
    processor.setRateAndBufferSizeDetails (48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

    // A supplied album WAV makes release captures show the plugin on Matías's music.
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> input;
    if (const auto path = juce::SystemStats::getEnvironmentVariable ("CONTOUR_SNAPSHOT_INPUT", {}); path.isNotEmpty())
    {
        input.reset (formats.createReaderFor (juce::File (path)));
        REQUIRE (input != nullptr);
        REQUIRE_THAT (input->sampleRate, WithinAbs (48000.0, 0.01));
        REQUIRE (input->numChannels == 2);
    }

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    for (int block = 0; block < 60; ++block)
    {
        if (input != nullptr)
            REQUIRE (input->read (&buffer, 0, 512, (juce::int64) block * 512, true, true));
        else for (int i = 0; i < 512; ++i)
        {
            const auto t = std::fmod ((block * 512 + i) / 24000.0, 1.0);
            const auto v = (float) (0.8 * std::exp (-t * 3.0) * std::sin ((block * 512 + i) * 0.05));
            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v * 0.8f);
        }

        processor.processBlock (buffer, midi);
    }

    // CONTOUR_SNAPSHOT_PRESET picks a factory preset by index, to see other modules.
    PresetManager presets (processor);
    presets.load (juce::SystemStats::getEnvironmentVariable ("CONTOUR_SNAPSHOT_PRESET", "0").getIntValue());

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    const auto capture = juce::SystemStats::getEnvironmentVariable ("CONTOUR_SNAPSHOT_MODE", "") == "capture";
    if (capture)
    {
        juce::TextButton* captureButton = nullptr;
        for (auto* child : editor->getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*> (child))
                if (button->getButtonText() == "Capture")
                    captureButton = button;

        REQUIRE (captureButton != nullptr);
        captureButton->onClick();
        CHECK (captureButton->getButtonText() == "Edit");
        CHECK (editor->getWidth() == 720);
        CHECK (editor->getHeight() == 1280);
        captureButton->onClick();
        CHECK (captureButton->getButtonText() == "Capture");
        CHECK (editor->getWidth() == 1060);
        CHECK (editor->getHeight() == 700);
        captureButton->onClick();
    }

    const auto defaultWidth = capture ? "720" : "1060";
    const auto defaultHeight = capture ? "1280" : "700";
    editor->setSize (juce::SystemStats::getEnvironmentVariable ("CONTOUR_SNAPSHOT_WIDTH", defaultWidth).getIntValue(),
                     juce::SystemStats::getEnvironmentVariable ("CONTOUR_SNAPSHOT_HEIGHT", defaultHeight).getIntValue());

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
    const auto path = juce::SystemStats::getEnvironmentVariable ("CONTOUR_SNAPSHOT", "/tmp/contour.png");
    juce::File (path).deleteFile();
    juce::FileOutputStream stream { juce::File (path) };
    REQUIRE (juce::PNGImageFormat().writeImageToStream (image, stream));
}
