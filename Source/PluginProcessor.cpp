#include "PluginProcessor.h"
#include "PluginEditor.h"

DawBridgeAudioProcessor::DawBridgeAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

//==============================================================================
void DawBridgeAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);

    currentSampleRate = sampleRate;

    const auto channels = juce::jmax (2, getTotalNumOutputChannels());
    webAudioFifo.reset (channels, (int) sampleRate); // ~1 second of headroom
}

void DawBridgeAudioProcessor::releaseResources()
{
}

bool DawBridgeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

//==============================================================================
void DawBridgeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    buffer.clear();

    // Host -> Web: stash incoming MIDI so the editor can relay it to the page as
    // synthetic Web MIDI input.
    if (! midiMessages.isEmpty())
    {
        const juce::ScopedLock sl (hostToWebMidiLock);
        hostToWebMidi.addEvents (midiMessages, 0, buffer.getNumSamples(), 0);
    }

    // Web -> Host: emit whatever MIDI the page queued via its synthetic MIDI output.
    midiMessages.clear();
    {
        const juce::ScopedLock sl (webToHostMidiLock);
        midiMessages.swapWith (webToHostMidi);
    }

    // Web -> Host audio: pull whatever has arrived from the browser and mix it
    // into the host's output buffer.
    juce::AudioBuffer<float> webAudio (buffer.getNumChannels(), buffer.getNumSamples());
    webAudio.clear();
    const auto framesRead = webAudioFifo.popInto (webAudio, buffer.getNumSamples());

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.addFrom (ch, 0, webAudio, ch, 0, framesRead);
}

//==============================================================================
juce::AudioProcessorEditor* DawBridgeAudioProcessor::createEditor()
{
    return new DawBridgeAudioProcessorEditor (*this);
}

//==============================================================================
void DawBridgeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("DAWBRIDGE_STATE");
    state.setProperty ("url", getSavedUrl(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void DawBridgeAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        setSavedUrl (state.getProperty ("url", juce::String()).toString());
    }
}

//==============================================================================
void DawBridgeAudioProcessor::pushWebAudio (const float* interleaved, int numFrames, int numChannels)
{
    webAudioFifo.pushInterleaved (interleaved, numFrames, numChannels);
}

void DawBridgeAudioProcessor::pushWebMidi (const juce::MidiMessage& message)
{
    const juce::ScopedLock sl (webToHostMidiLock);
    webToHostMidi.addEvent (message, 0);
}

std::vector<juce::MidiMessage> DawBridgeAudioProcessor::drainHostMidiForWeb()
{
    std::vector<juce::MidiMessage> result;

    const juce::ScopedLock sl (hostToWebMidiLock);

    for (const auto metadata : hostToWebMidi)
        result.push_back (metadata.getMessage());

    hostToWebMidi.clear();
    return result;
}

DawBridgeAudioProcessor::TransportSnapshot DawBridgeAudioProcessor::getTransportSnapshot() const
{
    TransportSnapshot snapshot;
    snapshot.sampleRate = currentSampleRate;

    if (auto* currentPlayHead = getPlayHead())
    {
        if (auto position = currentPlayHead->getPosition())
        {
            snapshot.bpm = position->getBpm().orFallback (120.0);
            snapshot.ppqPosition = position->getPpqPosition().orFallback (0.0);
            snapshot.isPlaying = position->getIsPlaying();
            snapshot.isRecording = position->getIsRecording();
        }
    }

    return snapshot;
}

juce::String DawBridgeAudioProcessor::getSavedUrl() const
{
    const juce::ScopedLock sl (savedUrlLock);
    return savedUrl;
}

void DawBridgeAudioProcessor::setSavedUrl (const juce::String& url)
{
    const juce::ScopedLock sl (savedUrlLock);
    savedUrl = url;
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DawBridgeAudioProcessor();
}
