#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Curve.h"

class ContourProcessor : public juce::AudioProcessor
{
public:
    ContourProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

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

    // Message thread: call after editing the curve so the host marks the session dirty.
    void curveChanged();

    static juce::StringArray rateNames();

    juce::AudioProcessorValueTreeState apvts;
    Curve curve;

    // Written by the audio thread, read by the editor for the playhead.
    std::atomic<float> displayPhase { 0.0f };
    std::atomic<float> displayGain { 1.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static double beatsPerCycle (int rateIndex);

    std::atomic<float>* rateParam = nullptr;
    std::atomic<float>* depthParam = nullptr;
    std::atomic<float>* smoothParam = nullptr;
    std::atomic<float>* offsetParam = nullptr;

    double currentSampleRate = 44100.0;
    double freeRunPpq = 0.0;
    float smoothedGain = 1.0f;

    JUCE_DECLARE_WEAK_REFERENCEABLE (ContourProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ContourProcessor)
};
