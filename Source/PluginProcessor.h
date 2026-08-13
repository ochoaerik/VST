#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

#include "WebAudioFifo.h"

/** The DSP/host side of WebTap.

    WebTap runs a page inside an embedded browser (see PluginEditor) and
    bridges it into the host as an instrument plugin:

      - audio the page renders is captured (via a JS/AudioWorklet shim
        injected into the page) and pushed here through pushWebAudio(),
        where it is mixed into the host's output buffer;
      - MIDI the host sends to this plugin is queued for the editor to
        relay to the page as synthetic Web MIDI input;
      - MIDI the page sends back is queued here via pushWebMidi() and
        emitted to the host on the next processBlock();
      - host transport (tempo, playhead, play/record state) is exposed to
        the page via getTransportSnapshot().

    All of the push/pop methods are safe to call from the message thread
    (where the WebBrowserComponent callbacks run) concurrently with
    processBlock() running on the audio thread.
*/
class WebTapAudioProcessor : public juce::AudioProcessor
{
public:
    WebTapAudioProcessor();
    ~WebTapAudioProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==============================================================================
    /** Called (from the message thread) when the page's capture worklet hands us
        a chunk of interleaved PCM. */
    void pushWebAudio (const float* interleaved, int numFrames, int numChannels);

    /** Called (from the message thread) when the page sends a MIDI message via
        its synthetic Web MIDI output. */
    void pushWebMidi (const juce::MidiMessage& message);

    /** Drains MIDI the host has sent to this plugin so the editor can relay it to
        the page as synthetic Web MIDI input events. Call from the message thread. */
    std::vector<juce::MidiMessage> drainHostMidiForWeb();

    struct TransportSnapshot
    {
        double bpm = 120.0;
        double ppqPosition = 0.0;
        bool isPlaying = false;
        bool isRecording = false;
        double sampleRate = 44100.0;
    };

    TransportSnapshot getTransportSnapshot() const;

    /** The last URL the editor navigated to. Persisted in the plugin state so a
        saved DAW project reopens the same page. */
    juce::String getSavedUrl() const;
    void setSavedUrl (const juce::String& url);

private:
    WebAudioFifo webAudioFifo;

    juce::MidiBuffer webToHostMidi;
    juce::CriticalSection webToHostMidiLock;

    juce::MidiBuffer hostToWebMidi;
    juce::CriticalSection hostToWebMidiLock;

    juce::String savedUrl;
    mutable juce::CriticalSection savedUrlLock;

    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WebTapAudioProcessor)
};
