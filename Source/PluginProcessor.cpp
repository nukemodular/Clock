#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
ClockSyncAudioProcessor::ClockSyncAudioProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true) // stereo out
                           ),
      parameters(*this, nullptr, juce::Identifier("ClockSyncParams"), createParameterLayout())
{
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout ClockSyncAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        paramClockResolution, "Clock Resolution", juce::StringArray{ "24", "48", "96" }, 2)); // default 96

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        paramClickEnable, "Enable Audio Click", false));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramClickLevelDb, "Click Level (dB)", juce::NormalisableRange<float>(-12.0f, 0.0f), -6.0f));

    return { params.begin(), params.end() };
}

//==============================================================================
void ClockSyncAudioProcessor::prepareToPlay(double sr, int /*samplesPerBlock*/)
{
    currentSampleRate = sr > 0.0 ? sr : 44100.0;
    lastWasPlaying = false;
    lastTickIndex = std::numeric_limits<long long>::min();
    clickEnv = 0.0f;
    updateDerivedParams();
}

void ClockSyncAudioProcessor::releaseResources() {}

bool ClockSyncAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // No input, stereo output only
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::disabled())
        return false;
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    return true;
}

//==============================================================================
int ClockSyncAudioProcessor::getClockResolution() const
{
    auto* p = parameters.getParameter(paramClockResolution);
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(p))
    {
        const juce::String text = choice->getCurrentChoiceName();
        return text.getIntValue();
    }
    return 96;
}

void ClockSyncAudioProcessor::updateDerivedParams()
{
    const auto levelDb = parameters.getRawParameterValue(paramClickLevelDb)->load();
    clickGainLinear = juce::Decibels::decibelsToGain(levelDb);
}

//==============================================================================
void ClockSyncAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    // Clear buffer; we'll add click if enabled
    buffer.clear();

    juce::AudioPlayHead::CurrentPositionInfo pos;
    const bool hasPos = (getPlayHead() != nullptr) && getPlayHead()->getCurrentPosition(pos);

    if (! hasPos)
    {
        // No transport info; just decay any existing click
        if (parameters.getRawParameterValue(paramClickEnable)->load() > 0.5f)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                float s = clickEnv;
                clickEnv *= 0.995f;
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    buffer.addSample(ch, i, s * clickGainLinear);
            }
        }
        return;
    }

    generateClockAndClick(pos, buffer, midi);
}

void ClockSyncAudioProcessor::generateClockAndClick(const juce::AudioPlayHead::CurrentPositionInfo& pos,
                                                    juce::AudioBuffer<float>& buffer,
                                                    juce::MidiBuffer& midi)
{
    const int numSamples = buffer.getNumSamples();

    // Update derived params (fast, read atomics)
    updateDerivedParams();
    const bool clickEnabled = parameters.getRawParameterValue(paramClickEnable)->load() > 0.5f;
    const int resolution = getClockResolution(); // e.g., 96

    const double bpm = (pos.bpm > 0.0 ? pos.bpm : 120.0);
    samplesPerQuarter = currentSampleRate * 60.0 / juce::jmax(1e-6, bpm);

    const double ppqStart = pos.ppqPosition;
    const bool isPlaying = pos.isPlaying;

    // Transport state transitions: emit Start/Continue/Stop
    if (isPlaying && ! lastWasPlaying)
    {
        // On play start, decide Start vs Continue based on position
        if (ppqStart < 1e-6)
            midi.addEvent(juce::MidiMessage::midiStart(), 0);
        else
            midi.addEvent(juce::MidiMessage::midiContinue(), 0);

        // Optional: could send Song Position Pointer here based on ppqStart
        // const int sppUnits = (int) juce::jmax(0, (int) std::floor(ppqStart * 4.0));
        // midi.addEvent(juce::MidiMessage::songPositionPointer(sppUnits), 0);
    }
    else if (! isPlaying && lastWasPlaying)
    {
        midi.addEvent(juce::MidiMessage::midiStop(), 0);
    }

    // Generate clock ticks aligned to transport when playing
    if (isPlaying && std::isfinite(ppqStart))
    {
        const double ppqPerSample = 1.0 / samplesPerQuarter; // quarter-notes per sample

        // Determine tick index at start and end of block
        auto tickAt = [resolution](double ppq) -> long long { return (long long) std::floor(ppq * (double) resolution); };

        const long long tickIndexStart = tickAt(ppqStart);
        const long long tickIndexEnd   = tickAt(ppqStart + (double) (numSamples - 1) * ppqPerSample);

        // Ensure lastTickIndex is sane relative to current block
        if (lastTickIndex == std::numeric_limits<long long>::min())
            lastTickIndex = tickIndexStart - 1;

        // Emit any missing ticks that occur within this block
        for (long long t = juce::jmax(lastTickIndex + 1, tickIndexStart); t <= tickIndexEnd; ++t)
        {
            const double tickPPQ = (double) t / (double) resolution;
            const double deltaQuarter = tickPPQ - ppqStart;
            const int sampleOffset = juce::jlimit(0, numSamples - 1, (int) std::llround(deltaQuarter * samplesPerQuarter));
            midi.addEvent(juce::MidiMessage::midiClock(), sampleOffset);

            // Trigger click on quarter-note boundaries (t multiple of resolution)
            if (clickEnabled && (t % resolution) == 0)
                clickEnv = 1.0f;
        }

        lastTickIndex = tickIndexEnd;
    }

    // Audio click synthesis (simple decaying impulse)
    if (clickEnabled)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float s = clickEnv;
            // Fast exponential decay for a sharp tick
            clickEnv *= 0.995f;

            s *= clickGainLinear;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.addSample(ch, i, s);
        }
    }

    lastWasPlaying = isPlaying;
}

//==============================================================================
bool ClockSyncAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* ClockSyncAudioProcessor::createEditor()
{
    return new ClockSyncAudioProcessorEditor(*this);
}

//==============================================================================
void ClockSyncAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void ClockSyncAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        if (xml->hasTagName(parameters.state.getType()))
        {
            parameters.replaceState(juce::ValueTree::fromXml(*xml));
            updateDerivedParams();
        }
    }
}

//==============================================================================
// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ClockSyncAudioProcessor();
}
