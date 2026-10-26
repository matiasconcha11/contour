#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

ContourProcessor::ContourProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", Params::createLayout())
{
    for (int m = 0; m < Params::numModules; ++m)
    {
        moduleParams[(size_t) m].on = apvts.getRawParameterValue (Params::onId (m));
        moduleParams[(size_t) m].depth = apvts.getRawParameterValue (Params::depthId (m));
    }

    rateParam = apvts.getRawParameterValue (Params::rate);
    rateHzParam = apvts.getRawParameterValue (Params::rateHz);
    syncParam = apvts.getRawParameterValue (Params::sync);
    offsetParam = apvts.getRawParameterValue (Params::offset);
    smoothParam = apvts.getRawParameterValue (Params::smooth);
    mixParam = apvts.getRawParameterValue (Params::mix);
    inputGainParam = apvts.getRawParameterValue (Params::inputGain);
    outputGainParam = apvts.getRawParameterValue (Params::outputGain);
    bypassParam = apvts.getRawParameterValue (Params::bypass);

    curves[Params::volume].setPoints (Curve::preset (Curve::duck));
    curves[Params::pan].setPoints (Curve::preset (Curve::sine));
    curves[Params::width].setPoints (Curve::preset (Curve::sine));

    for (auto* param : getParameters())
        param->addListener (this);
}

ContourProcessor::~ContourProcessor()
{
    for (auto* param : getParameters())
        param->removeListener (this);
}

juce::AudioProcessorParameter* ContourProcessor::getBypassParameter() const
{
    return apvts.getParameter (Params::bypass);
}

void ContourProcessor::parameterValueChanged (int parameterIndex, float)
{
    if (loadingPreset || getParameters()[parameterIndex] == getBypassParameter())
        return;

    presetDirty = true;
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
    smoothedPan = 0.0f;
    smoothedWidth = 1.0f;

    const auto bypassed = bypassParam->load() > 0.5f;
    mixSmoothed.reset (sampleRate, 0.03);
    mixSmoothed.setCurrentAndTargetValue (bypassed ? 0.0f : mixParam->load());
    inputGainSmoothed.reset (sampleRate, 0.03);
    inputGainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (inputGainParam->load()));
    outputGainSmoothed.reset (sampleRate, 0.03);
    outputGainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputGainParam->load()));
}

void ContourProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    double bpm = 120.0;
    double beatsPerBar = 4.0;
    double ppq = freeRunPpq;
    bool hostPlaying = false;

    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            if (auto hostBpm = position->getBpm(); hostBpm && *hostBpm > 0.0)
                bpm = *hostBpm;

            if (auto sig = position->getTimeSignature(); sig && sig->denominator > 0)
                beatsPerBar = sig->numerator * 4.0 / sig->denominator;

            if (auto hostPpq = position->getPpqPosition(); hostPpq && position->getIsPlaying())
            {
                ppq = *hostPpq;
                hostPlaying = true;
            }
        }
    }

    const auto numSamples = buffer.getNumSamples();
    const auto stereo = getTotalNumOutputChannels() > 1;
    auto* left = buffer.getWritePointer (0);
    auto* right = stereo ? buffer.getWritePointer (1) : nullptr;

    // Timing: phase of the first sample and the per-sample increment, in cycles.
    const auto sync = syncParam->load() > 0.5f;
    const auto cycleBeats = Params::beatsPerCycle ((int) rateParam->load(), beatsPerBar);
    const auto ppqPerSample = bpm / 60.0 / currentSampleRate;
    const auto startPhase = sync ? ppq / cycleBeats : hzPhase;
    const auto phaseIncrement = sync ? ppqPerSample / cycleBeats : rateHzParam->load() / currentSampleRate;
    const auto offset = (double) offsetParam->load();

    bool on[Params::numModules];
    float depth[Params::numModules];

    for (int m = 0; m < Params::numModules; ++m)
    {
        on[m] = moduleParams[(size_t) m].on->load() > 0.5f;
        depth[m] = moduleParams[(size_t) m].depth->load();
    }

    const auto smoothMs = smoothParam->load();
    const auto smoothCoeff = smoothMs > 0.0f
                                 ? (float) std::exp (-1.0 / (smoothMs * 0.001 * currentSampleRate))
                                 : 0.0f;

    mixSmoothed.setTargetValue (bypassParam->load() > 0.5f ? 0.0f : mixParam->load());
    inputGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (inputGainParam->load()));
    outputGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (outputGainParam->load()));

    auto phase = 0.0;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto cyclePos = startPhase + i * phaseIncrement + offset;
        phase = cyclePos - std::floor (cyclePos);
        const auto p = (float) phase;

        const auto gainTarget = on[Params::volume] ? 1.0f - depth[Params::volume] * (1.0f - curves[Params::volume].lookup (p)) : 1.0f;
        const auto panTarget = on[Params::pan] ? (curves[Params::pan].lookup (p) - 0.5f) * 2.0f * depth[Params::pan] : 0.0f;
        const auto widthTarget = on[Params::width] ? 1.0f + (curves[Params::width].lookup (p) - 0.5f) * 2.0f * depth[Params::width] : 1.0f;

        smoothedGain = gainTarget + smoothCoeff * (smoothedGain - gainTarget);
        smoothedPan = panTarget + smoothCoeff * (smoothedPan - panTarget);
        smoothedWidth = widthTarget + smoothCoeff * (smoothedWidth - widthTarget);

        const auto mix = mixSmoothed.getNextValue();
        const auto inGain = inputGainSmoothed.getNextValue();
        const auto outGain = outputGainSmoothed.getNextValue();

        const auto dryL = left[i];
        const auto dryR = stereo ? right[i] : dryL;
        auto wetL = dryL * inGain;
        auto wetR = dryR * inGain;

        if (stereo)
        {
            const auto mid = (wetL + wetR) * 0.5f;
            const auto side = (wetL - wetR) * 0.5f * smoothedWidth;
            wetL = mid + side;
            wetR = mid - side;

            // Equal-power pan, normalised to unity in the centre.
            const auto angle = (smoothedPan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            wetL *= std::cos (angle) * juce::MathConstants<float>::sqrt2;
            wetR *= std::sin (angle) * juce::MathConstants<float>::sqrt2;
        }

        wetL *= smoothedGain;
        wetR *= smoothedGain;

        const auto outL = (dryL + (wetL - dryL) * mix) * outGain;
        const auto outR = (dryR + (wetR - dryR) * mix) * outGain;
        left[i] = outL;

        if (stereo)
            right[i] = outR;

        // Waveform display: one peak per bin of the cycle.
        const auto bin = juce::jmin ((int) (p * (float) waveformBins), waveformBins - 1);

        if (bin != waveBin)
        {
            waveIn[(size_t) waveBin].store (waveInPeak, std::memory_order_relaxed);
            waveOut[(size_t) waveBin].store (waveOutPeak, std::memory_order_relaxed);
            waveInPeak = waveOutPeak = 0.0f;
            waveBin = bin;
        }

        waveInPeak = juce::jmax (waveInPeak, std::abs (dryL), std::abs (dryR));
        waveOutPeak = juce::jmax (waveOutPeak, std::abs (outL), std::abs (outR));
    }

    // Keep the clocks running so the shape still moves when the transport is stopped.
    const auto endPpq = ppq + numSamples * ppqPerSample;
    freeRunPpq = hostPlaying ? endPpq : std::fmod (endPpq, cycleBeats);
    const auto endHzPhase = hzPhase + numSamples * (rateHzParam->load() / currentSampleRate);
    hzPhase = endHzPhase - std::floor (endHzPhase);

    displayPhase.store ((float) phase, std::memory_order_relaxed);
}

void ContourProcessor::curveChanged()
{
    presetDirty = true;
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

juce::ValueTree ContourProcessor::getStateTree()
{
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName ("CURVES"), nullptr);

    juce::ValueTree curveTrees ("CURVES");

    for (int m = 0; m < Params::numModules; ++m)
    {
        auto tree = curves[(size_t) m].toValueTree();
        tree.setProperty ("module", Params::moduleIds[m], nullptr);
        curveTrees.appendChild (tree, nullptr);
    }

    state.appendChild (curveTrees, nullptr);
    state.setProperty ("presetName", presetName, nullptr);
    state.setProperty ("presetDirty", isPresetDirty(), nullptr);
    return state;
}

void ContourProcessor::applyParameterState (const juce::ValueTree& source)
{
    auto state = source.createCopy();
    state.removeChild (state.getChildWithName ("CURVES"), nullptr);
    state.removeChild (state.getChildWithName ("CURVE"), nullptr);

    {
        const ScopedPresetLoad loading (*this);
        apvts.replaceState (state);
    }

    presetDirty = (bool) source.getProperty ("presetDirty", false);
}

void ContourProcessor::applyCurveState (const juce::ValueTree& state)
{
    if (const auto curveTrees = state.getChildWithName ("CURVES"); curveTrees.isValid())
    {
        for (const auto& tree : curveTrees)
        {
            const auto module = Params::moduleIds.indexOf (tree.getProperty ("module").toString());

            if (module >= 0)
                curves[(size_t) module].fromValueTree (tree);
        }
    }
    else if (const auto legacy = state.getChildWithName ("CURVE"); legacy.isValid())
    {
        // Sessions from the first version had a single volume curve.
        curves[Params::volume].fromValueTree (legacy);
    }

    presetName = state.getProperty ("presetName", "Init").toString();
}

void ContourProcessor::setStateTree (const juce::ValueTree& state)
{
    if (! state.hasType (apvts.state.getType()))
        return;

    applyCurveState (state);
    applyParameterState (state);
}

void ContourProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = getStateTree().createXml())
        copyXmlToBinary (*xml, destData);
}

void ContourProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    const auto state = juce::ValueTree::fromXml (*xml);

    // Parameters must be restored before this returns, whichever thread the host calls from.
    applyParameterState (state);

    // Curves and the preset name are owned by the message thread.
    const auto applyCurves = [this, state]
    {
        applyCurveState (state);

        if (auto* ed = dynamic_cast<ContourEditor*> (getActiveEditor()))
            ed->stateReloaded();
    };

    if (juce::MessageManager::existsAndIsCurrentThread())
        applyCurves();
    else
        juce::MessageManager::callAsync ([weakThis = juce::WeakReference<ContourProcessor> (this), applyCurves]
        {
            if (weakThis != nullptr)
                applyCurves();
        });
}

juce::AudioProcessorEditor* ContourProcessor::createEditor()
{
    return new ContourEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ContourProcessor();
}
