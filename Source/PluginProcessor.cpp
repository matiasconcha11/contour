#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

ContourProcessor::ContourProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    rateParam = apvts.getRawParameterValue ("rate");
    depthParam = apvts.getRawParameterValue ("depth");
    smoothParam = apvts.getRawParameterValue ("smooth");
    offsetParam = apvts.getRawParameterValue ("offset");
}

juce::StringArray ContourProcessor::rateNames()
{
    return { "1/32", "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars" };
}

double ContourProcessor::beatsPerCycle (int rateIndex)
{
    static constexpr double beats[] = { 0.125, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };
    return beats[juce::jlimit (0, (int) std::size (beats) - 1, rateIndex)];
}

juce::AudioProcessorValueTreeState::ParameterLayout ContourProcessor::createLayout()
{
    using namespace juce;

    return {
        std::make_unique<AudioParameterChoice> (ParameterID { "rate", 1 }, "Rate", rateNames(), 3),
        std::make_unique<AudioParameterFloat> (ParameterID { "depth", 1 }, "Depth",
                                               NormalisableRange<float> (0.0f, 1.0f), 1.0f),
        std::make_unique<AudioParameterFloat> (ParameterID { "smooth", 1 }, "Smooth",
                                               NormalisableRange<float> (0.0f, 20.0f, 0.0f, 0.5f), 1.5f,
                                               AudioParameterFloatAttributes().withLabel ("ms")),
        std::make_unique<AudioParameterFloat> (ParameterID { "offset", 1 }, "Offset",
                                               NormalisableRange<float> (0.0f, 1.0f), 0.0f)
    };
}

bool ContourProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return out == layouts.getMainInputChannelSet();
}

void ContourProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    smoothedGain = 1.0f;
}

void ContourProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    double bpm = 120.0;
    double ppq = freeRunPpq;
    bool hostPlaying = false;

    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            if (auto hostBpm = position->getBpm())
                bpm = *hostBpm;

            if (auto hostPpq = position->getPpqPosition(); hostPpq && position->getIsPlaying())
            {
                ppq = *hostPpq;
                hostPlaying = true;
            }
        }
    }

    const auto cycleBeats = beatsPerCycle ((int) rateParam->load());
    const auto depth = depthParam->load();
    const auto offset = (double) offsetParam->load();
    const auto ppqPerSample = bpm / 60.0 / currentSampleRate;

    const auto smoothMs = smoothParam->load();
    const auto smoothCoeff = smoothMs > 0.0f
                                 ? (float) std::exp (-1.0 / (smoothMs * 0.001 * currentSampleRate))
                                 : 0.0f;

    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = getTotalNumOutputChannels();
    auto phase = 0.0;

    for (int i = 0; i < numSamples; ++i)
    {
        phase = std::fmod (ppq / cycleBeats + offset, 1.0);
        const auto target = 1.0f - depth * (1.0f - curve.lookup ((float) phase));
        smoothedGain = target + smoothCoeff * (smoothedGain - target);

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.getWritePointer (ch)[i] *= smoothedGain;

        ppq += ppqPerSample;
    }

    // Keep the internal clock moving so the shape still runs when the transport is stopped.
    freeRunPpq = hostPlaying ? ppq : std::fmod (ppq, 64.0);

    displayPhase.store ((float) phase, std::memory_order_relaxed);
    displayGain.store (smoothedGain, std::memory_order_relaxed);
}

void ContourProcessor::curveChanged()
{
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

void ContourProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName ("CURVE"), nullptr);
    state.appendChild (curve.toValueTree(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ContourProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    const auto curveTree = state.getChildWithName ("CURVE");

    // The curve is owned by the message thread; hosts almost always restore state there.
    const auto applyCurve = [this, curveTree]
    {
        curve.fromValueTree (curveTree);

        if (auto* ed = dynamic_cast<ContourEditor*> (getActiveEditor()))
            ed->curveReloaded();
    };

    if (juce::MessageManager::existsAndIsCurrentThread())
        applyCurve();
    else
        juce::MessageManager::callAsync ([weakThis = juce::WeakReference<ContourProcessor> (this), applyCurve]
        {
            if (weakThis != nullptr)
                applyCurve();
        });

    state.removeChild (curveTree, nullptr);
    apvts.replaceState (state);
}

juce::AudioProcessorEditor* ContourProcessor::createEditor()
{
    return new ContourEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ContourProcessor();
}
