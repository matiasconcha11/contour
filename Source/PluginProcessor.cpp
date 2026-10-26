#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <limits>
#include <optional>

namespace
{
    // A module's modulation value in its own units; switched-off modules sit at their neutral value.
    float moduleValue (int module, bool on, float curve, float depth, int filterType)
    {
        switch (module)
        {
            case Params::volume: return on ? 1.0f - depth * (1.0f - curve) : 1.0f;
            case Params::pan: return on ? (curve - 0.5f) * 2.0f * depth : 0.0f;
            case Params::width: return on ? 1.0f + (curve - 0.5f) * 2.0f * depth : 1.0f;
            case Params::filter:
                // Normalised cutoff; depth pulls it towards the setting that lets everything through.
                if (filterType == 1) return curve * depth;
                if (filterType == 2) return 0.5f + (curve - 0.5f) * depth;
                return 1.0f - depth * (1.0f - curve);
            case Params::drive:
            case Params::crush: return on ? curve * depth : 0.0f;
            case Params::time: return on ? (1.0f - curve) * depth : 0.0f; // fraction of a cycle to look back
            default: return 0.0f;
        }
    }

    constexpr double maxDelaySeconds = 16.0;
}

ContourProcessor::ContourProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "PARAMS", Params::createLayout())
{
    for (int m = 0; m < Params::numModules; ++m)
    {
        moduleParams[(size_t) m].on = apvts.getRawParameterValue (Params::onId (m));
        moduleParams[(size_t) m].depth = apvts.getRawParameterValue (Params::depthId (m));
        curves[(size_t) m].setPoints (Params::defaultCurve (m));
    }

    modeParam = apvts.getRawParameterValue (Params::mode);
    rateParam = apvts.getRawParameterValue (Params::rate);
    rateHzParam = apvts.getRawParameterValue (Params::rateHz);
    thresholdParam = apvts.getRawParameterValue (Params::threshold);
    triggerSourceParam = apvts.getRawParameterValue (Params::triggerSource);
    offsetParam = apvts.getRawParameterValue (Params::offset);
    smoothParam = apvts.getRawParameterValue (Params::smooth);
    filterTypeParam = apvts.getRawParameterValue (Params::filterType);
    filterResonanceParam = apvts.getRawParameterValue (Params::filterResonance);
    mixParam = apvts.getRawParameterValue (Params::mix);
    inputGainParam = apvts.getRawParameterValue (Params::inputGain);
    outputGainParam = apvts.getRawParameterValue (Params::outputGain);
    bypassParam = apvts.getRawParameterValue (Params::bypass);

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

    if (out != layouts.getMainInputChannelSet())
        return false;

    // The optional sidechain (trigger source in Audio mode) can be off, mono or stereo.
    if (layouts.inputBuses.size() > 1)
    {
        const auto sidechain = layouts.getChannelSet (true, 1);
        return sidechain.isDisabled() || sidechain == juce::AudioChannelSet::mono() || sidechain == juce::AudioChannelSet::stereo();
    }

    return true;
}

void ContourProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;

    const auto filterType = (int) filterTypeParam->load();

    for (int m = 0; m < Params::numModules; ++m)
        smoothed[(size_t) m] = moduleValue (m, false, 0.0f, 0.0f, filterType);

    smoothed[Params::filter] = 1.0f;
    filterWet = moduleParams[Params::filter].on->load() > 0.5f ? 1.0f : 0.0f;

    for (auto& f : filters)
        f.reset();

    for (auto& d : delays)
        d.prepare ((int) (maxDelaySeconds * sampleRate));

    for (auto& c : crushers)
        c = {};

    triggerEnvelope = 0.0f;
    triggerArmed = true;
    samplesSinceTrigger = std::numeric_limits<int>::max() / 2; // the first hit after starting must count

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

    // Transport.
    double bpm = 120.0;
    double beatsPerBar = 4.0;
    bool hasHost = false;
    bool hostPlaying = false;
    std::optional<double> lastBarStart;

    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            hasHost = true;

            if (auto hostBpm = position->getBpm(); hostBpm && *hostBpm > 0.0)
                bpm = *hostBpm;

            if (auto sig = position->getTimeSignature(); sig && sig->denominator > 0)
                beatsPerBar = sig->numerator * 4.0 / sig->denominator;

            if (auto hostPpq = position->getPpqPosition(); hostPpq && position->getIsPlaying())
            {
                tempoPpq = *hostPpq;
                hostPlaying = true;

                if (auto barStart = position->getPpqPositionOfLastBarStart())
                    lastBarStart = *barStart;
            }
        }
    }

    const auto numSamples = buffer.getNumSamples();
    const auto ppqPerSample = bpm / 60.0 / currentSampleRate;

    const auto transportJumped = hostPlaying && (! hostWasPlaying || std::abs (tempoPpq - expectedHostPpq) > 0.01);
    hostWasPlaying = hostPlaying;
    expectedHostPpq = tempoPpq + numSamples * ppqPerSample;

    auto main = getBusBuffer (buffer, false, 0);
    const auto stereo = main.getNumChannels() > 1;
    auto* left = main.getWritePointer (0);
    auto* right = stereo ? main.getWritePointer (1) : nullptr;

    // Sidechain: only present when the host has connected something to it.
    const auto* sidechainBus = getBus (true, 1);
    const auto sidechainActive = sidechainBus != nullptr && sidechainBus->isEnabled() && getChannelCountOfBus (true, 1) > 0;
    const auto sidechain = sidechainActive ? getBusBuffer (buffer, true, 1) : juce::AudioBuffer<float>();
    sidechainConnected.store (sidechainActive, std::memory_order_relaxed);

    // Clock: phase of the first sample and the per-sample increment, in cycles.
    const auto mode = (int) modeParam->load();
    const auto rateIndex = (int) rateParam->load();
    const auto cycleBeats = Params::beatsPerCycle (rateIndex, beatsPerBar);
    const auto cycleSamples = mode == Params::hzMode ? currentSampleRate / rateHzParam->load()
                                                     : cycleBeats / ppqPerSample;

    // In Tempo mode the shape follows the host and freezes while it is stopped.
    // Without a host (the standalone app) it runs at 120 bpm.
    const auto tempoRunning = hostPlaying || ! hasHost;
    auto phaseAcc = mode == Params::tempoMode ? tempoPpq / cycleBeats : freePhase;

    // Bar-based rates count bars rather than beats, so cycles start on bar lines even after a
    // time-signature change. AU and VST3 hosts report where the current bar started but not its
    // number, so the bar number is estimated in the current signature (exact if it never changes).
    if (const auto bars = Params::barsPerCycle (rateIndex); mode == Params::tempoMode && bars > 0 && lastBarStart)
    {
        const auto barNumber = std::round (*lastBarStart / beatsPerBar);
        phaseAcc = (barNumber + (tempoPpq - *lastBarStart) / beatsPerBar) / bars;
    }
    const auto phaseIncrement = mode == Params::tempoMode ? (tempoRunning ? 1.0 / cycleSamples : 0.0)
                                                          : 1.0 / cycleSamples;
    const auto offset = (double) offsetParam->load();

    // Playback started or jumped: audio from before is no longer "the recent past" for the Time
    // module. It never reads back more than one cycle, so only that much needs silencing.
    if (transportJumped)
        for (auto& d : delays)
            d.clearRecent ((int) cycleSamples + 2);

    // Audio trigger: restart the cycle when the input crosses the threshold.
    const auto audioTrigger = mode == Params::audioMode;
    const auto useSidechain = (int) triggerSourceParam->load() == 1;
    const auto* sidechainL = sidechainActive ? sidechain.getReadPointer (0) : nullptr;
    const auto* sidechainR = sidechainActive ? sidechain.getReadPointer (sidechain.getNumChannels() > 1 ? 1 : 0) : nullptr;
    const auto threshold = juce::Decibels::decibelsToGain (thresholdParam->load());
    const auto envelopeRelease = (float) std::exp (-1.0 / (0.03 * currentSampleRate));
    const auto minTriggerGap = (int) (0.05 * currentSampleRate);

    bool on[Params::numModules];
    float depth[Params::numModules];

    for (int m = 0; m < Params::numModules; ++m)
    {
        on[m] = moduleParams[(size_t) m].on->load() > 0.5f;
        depth[m] = moduleParams[(size_t) m].depth->load();
    }

    const auto filterType = (int) filterTypeParam->load();
    const auto resonance = filterResonanceParam->load();
    const auto filterK = 1.0f / (0.5f + resonance * resonance * 11.5f);
    const auto nyquistLimit = (float) (currentSampleRate * 0.49);
    const auto filterWetTarget = on[Params::filter] ? 1.0f : 0.0f;
    const auto filterWetCoeff = (float) std::exp (-1.0 / (0.01 * currentSampleRate));

    const auto smoothMs = smoothParam->load();
    const auto smoothCoeff = smoothMs > 0.0f
                                 ? (float) std::exp (-1.0 / (smoothMs * 0.001 * currentSampleRate))
                                 : 0.0f;

    mixSmoothed.setTargetValue (bypassParam->load() > 0.5f ? 0.0f : mixParam->load());
    inputGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (inputGainParam->load()));
    outputGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (outputGainParam->load()));

    float peakLeft = 0.0f, peakRight = 0.0f;
    auto phase = 0.0;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto mix = mixSmoothed.getNextValue();
        const auto inGain = inputGainSmoothed.getNextValue();
        const auto outGain = outputGainSmoothed.getNextValue();

        const auto dryL = left[i];
        const auto dryR = stereo ? right[i] : dryL;
        auto l = dryL * inGain;
        auto r = dryR * inGain;

        if (audioTrigger)
        {
            // Detect on this track's audio, or on the sidechain (silence if nothing is connected).
            const auto detectL = useSidechain ? (sidechainL != nullptr ? sidechainL[i] : 0.0f) : l;
            const auto detectR = useSidechain ? (sidechainR != nullptr ? sidechainR[i] : 0.0f) : r;
            triggerEnvelope = juce::jmax (std::abs (detectL), std::abs (detectR), triggerEnvelope * envelopeRelease);
            ++samplesSinceTrigger;

            if (triggerArmed && triggerEnvelope >= threshold && samplesSinceTrigger >= minTriggerGap)
            {
                phaseAcc = 0.0;
                triggerArmed = false;
                samplesSinceTrigger = 0;
                triggerCount.fetch_add (1, std::memory_order_relaxed);
            }
            else if (triggerEnvelope < threshold * 0.5f)
            {
                triggerArmed = true;
            }
        }

        const auto cyclePos = phaseAcc + offset;
        phase = cyclePos - std::floor (cyclePos);
        phaseAcc += phaseIncrement;
        const auto p = (float) phase;

        for (int m = 0; m < Params::numModules; ++m)
        {
            const auto target = moduleValue (m, on[m], curves[(size_t) m].lookup (p), depth[m], filterType);
            smoothed[(size_t) m] = target + smoothCoeff * (smoothed[(size_t) m] - target);
        }

        // Time: read from the recent past.
        delays[0].push (l);
        delays[1].push (r);

        if (smoothed[Params::time] > 0.0f)
        {
            const auto delay = (float) (smoothed[Params::time] * cycleSamples);
            l = delays[0].read (delay);
            r = delays[1].read (delay);
        }

        // Filter, crossfaded in and out when the module is switched.
        filterWet = filterWetTarget + filterWetCoeff * (filterWet - filterWetTarget);

        if (filterWet > 1.0e-4f)
        {
            const auto cutoff = juce::jmin (nyquistLimit, 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, smoothed[Params::filter])));
            const auto g = std::tan (juce::MathConstants<float>::pi * cutoff / (float) currentSampleRate);
            l += (filters[0].process (l, g, filterK, filterType) - l) * filterWet;
            r += (filters[1].process (r, g, filterK, filterType) - r) * filterWet;
        }
        else
        {
            filters[0].reset();
            filters[1].reset();
        }

        l = Dsp::drive (l, smoothed[Params::drive]);
        r = Dsp::drive (r, smoothed[Params::drive]);
        l = crushers[0].process (l, smoothed[Params::crush]);
        r = crushers[1].process (r, smoothed[Params::crush]);

        if (stereo)
        {
            const auto mid = (l + r) * 0.5f;
            const auto side = (l - r) * 0.5f * smoothed[Params::width];
            l = mid + side;
            r = mid - side;

            // Equal-power pan, normalised to unity in the centre.
            const auto angle = (smoothed[Params::pan] + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            l *= std::cos (angle) * juce::MathConstants<float>::sqrt2;
            r *= std::sin (angle) * juce::MathConstants<float>::sqrt2;
        }

        l *= smoothed[Params::volume];
        r *= smoothed[Params::volume];

        const auto outL = (dryL + (l - dryL) * mix) * outGain;
        const auto outR = (dryR + (r - dryR) * mix) * outGain;
        left[i] = outL;

        if (stereo)
            right[i] = outR;

        peakLeft = juce::jmax (peakLeft, std::abs (outL));
        peakRight = juce::jmax (peakRight, std::abs (stereo ? outR : outL));

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

    // Advance the clocks for the next block.
    if (mode == Params::tempoMode)
    {
        if (tempoRunning)
            tempoPpq += numSamples * ppqPerSample;

        if (! hostPlaying)
            tempoPpq = std::fmod (tempoPpq, cycleBeats * 64.0);
    }
    else
    {
        freePhase = phaseAcc - std::floor (phaseAcc);
    }

    displayPhase.store ((float) phase, std::memory_order_relaxed);
    outputPeakLeft.store (juce::jmax (peakLeft, outputPeakLeft.load (std::memory_order_relaxed)), std::memory_order_relaxed);
    outputPeakRight.store (juce::jmax (peakRight, outputPeakRight.load (std::memory_order_relaxed)), std::memory_order_relaxed);
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

    // 0.2 had a Sync switch where 0.3 has Mode (Tempo / Hz / Audio).
    const auto sync = state.getChildWithProperty ("id", "sync");

    if (sync.isValid() && ! state.getChildWithProperty ("id", Params::mode).isValid())
    {
        juce::ValueTree modeParam ("PARAM");
        modeParam.setProperty ("id", Params::mode, nullptr);
        modeParam.setProperty ("value", (float) sync.getProperty ("value") > 0.5f ? Params::tempoMode : Params::hzMode, nullptr);
        state.appendChild (modeParam, nullptr);
    }

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
