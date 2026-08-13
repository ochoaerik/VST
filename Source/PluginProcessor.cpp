#include "PluginProcessor.h"
#include "PluginEditor.h"

WebTapAudioProcessor::WebTapAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

//==============================================================================
void WebTapAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);

    currentSampleRate = sampleRate;

    const auto channels = juce::jmax (2, getTotalNumOutputChannels());
    webAudioFifo.reset (channels, (int) sampleRate); // ~1 second of headroom
}

void WebTapAudioProcessor::releaseResources()
{
}

bool WebTapAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

//==============================================================================
void WebTapAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
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
juce::AudioProcessorEditor* WebTapAudioProcessor::createEditor()
{
    return new WebTapAudioProcessorEditor (*this);
}

//==============================================================================
void WebTapAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("WEBTAP_STATE");
    state.setProperty ("url", getSavedUrl(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void WebTapAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        setSavedUrl (state.getProperty ("url", juce::String()).toString());
    }
}

//==============================================================================
void WebTapAudioProcessor::pushWebAudio (const float* interleaved, int numFrames, int numChannels)
{
    webAudioFifo.pushInterleaved (interleaved, numFrames, numChannels);
}

void WebTapAudioProcessor::pushWebMidi (const juce::MidiMessage& message)
{
    const juce::ScopedLock sl (webToHostMidiLock);
    webToHostMidi.addEvent (message, 0);
}

std::vector<juce::MidiMessage> WebTapAudioProcessor::drainHostMidiForWeb()
{
    std::vector<juce::MidiMessage> result;

    const juce::ScopedLock sl (hostToWebMidiLock);

    for (const auto metadata : hostToWebMidi)
        result.push_back (metadata.getMessage());

    hostToWebMidi.clear();
    return result;
}

WebTapAudioProcessor::TransportSnapshot WebTapAudioProcessor::getTransportSnapshot() const
{
    TransportSnapshot snapshot;
    snapshot.sampleRate = currentSampleRate;

    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            snapshot.bpm = position->getBpm().orFallback (120.0);
            snapshot.ppqPosition = position->getPpqPosition().orFallback (0.0);
            snapshot.isPlaying = position->getIsPlaying();
            snapshot.isRecording = position->getIsRecording();
        }
    }

    return snapshot;
}

juce::String WebTapAudioProcessor::getSavedUrl() const
{
    const juce::ScopedLock sl (savedUrlLock);
    return savedUrl;
}

void WebTapAudioProcessor::setSavedUrl (const juce::String& url)
{
    const juce::ScopedLock sl (savedUrlLock);
    savedUrl = url;
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new WebTapAudioProcessor();
}
