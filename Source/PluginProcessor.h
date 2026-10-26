#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Curve.h"
#include "Dsp.h"
#include "Parameters.h"

class ContourProcessor : public juce::AudioProcessor,
                         private juce::AudioProcessorParameter::Listener
{
public:
    static constexpr int waveformBins = 256;

    ContourProcessor();
    ~ContourProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Contour"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Full state (parameters + curves + preset name) as a tree. Message thread only.
    juce::ValueTree getStateTree();
    void setStateTree (const juce::ValueTree& state);

    // Message thread: call after editing a curve so the host and preset bar know.
    void curveChanged();

    // Preset bookkeeping, owned by the message thread.
    juce::String presetName { "Init" };
    bool isPresetDirty() const { return presetDirty.load(); }
    void markPresetClean() { presetDirty = false; }

    // Suppresses dirty tracking while a preset is being applied.
    struct ScopedPresetLoad
    {
        explicit ScopedPresetLoad (ContourProcessor& p) : processor (p) { processor.loadingPreset = true; }
        ~ScopedPresetLoad() { processor.loadingPreset = false; processor.presetDirty = false; }
        ContourProcessor& processor;
    };

    juce::AudioProcessorValueTreeState apvts;
    std::array<Curve, Params::numModules> curves;

    // Curve edits are undoable; parameter changes are left to the host's own undo.
    juce::UndoManager undoManager;

    // Written by the audio thread, read by the editor.
    std::atomic<float> displayPhase { 0.0f };
    std::array<std::atomic<float>, waveformBins> waveIn {};
    std::array<std::atomic<float>, waveformBins> waveOut {};
    std::atomic<float> outputPeakLeft { 0.0f }, outputPeakRight { 0.0f };
    std::atomic<int> triggerCount { 0 };
    std::atomic<bool> sidechainConnected { false };

private:
    void applyParameterState (const juce::ValueTree&);
    void applyCurveState (const juce::ValueTree&);

    void parameterValueChanged (int parameterIndex, float newValue) override;
    void parameterGestureChanged (int, bool) override {}

    struct ModuleParams
    {
        std::atomic<float>* on = nullptr;
        std::atomic<float>* depth = nullptr;
    };

    std::array<ModuleParams, Params::numModules> moduleParams;
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* rateParam = nullptr;
    std::atomic<float>* rateHzParam = nullptr;
    std::atomic<float>* thresholdParam = nullptr;
    std::atomic<float>* triggerSourceParam = nullptr;
    std::atomic<float>* offsetParam = nullptr;
    std::atomic<float>* smoothParam = nullptr;
    std::atomic<float>* filterTypeParam = nullptr;
    std::atomic<float>* filterResonanceParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* inputGainParam = nullptr;
    std::atomic<float>* outputGainParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;

    double currentSampleRate = 44100.0;

    // Clocks. tempoPpq tracks the host (or runs on its own without one); freePhase drives Hz and Audio modes.
    double tempoPpq = 0.0;
    double freePhase = 0.0;

    // Where the host's transport should be at the next block if it keeps playing; a mismatch is a jump.
    double expectedHostPpq = 0.0;
    bool hostWasPlaying = false;

    // Audio trigger detection.
    float triggerEnvelope = 0.0f;
    bool triggerArmed = true;
    int samplesSinceTrigger = 0;

    // Modulation values after click smoothing, in each module's own units.
    std::array<float, Params::numModules> smoothed {};
    float filterWet = 0.0f;

    std::array<Dsp::StateVariableFilter, 2> filters;
    std::array<Dsp::DelayLine, 2> delays;
    std::array<Dsp::Crusher, 2> crushers;
    juce::SmoothedValue<float> mixSmoothed, inputGainSmoothed, outputGainSmoothed;

    int waveBin = 0;
    float waveInPeak = 0.0f, waveOutPeak = 0.0f;

    std::atomic<bool> presetDirty { false };
    std::atomic<bool> loadingPreset { false };

    JUCE_DECLARE_WEAK_REFERENCEABLE (ContourProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ContourProcessor)
};
