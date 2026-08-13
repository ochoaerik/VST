#include "PluginEditor.h"

namespace
{
    // The page loaded on first run, before any URL has been saved into the
    // plugin's state (see DawBridgeAudioProcessor::getSavedUrl/setSavedUrl).
    const juce::String defaultAppUrl = "https://daw.streetmoguldistro.com";
}

DawBridgeAudioProcessorEditor::DawBridgeAudioProcessorEditor (DawBridgeAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    addAndMakeVisible (urlBarLabel);

    addAndMakeVisible (urlBar);
    urlBar.setTextToShowWhenEmpty ("https://your-web-audio-app.example", juce::Colours::grey);
    urlBar.onReturnKey = [this] { loadUrl (urlBar.getText()); };

    addAndMakeVisible (goButton);
    goButton.onClick = [this] { loadUrl (urlBar.getText()); };

    addAndMakeVisible (homeButton);
    homeButton.onClick = [this] { loadUrl (defaultAppUrl); };

    addAndMakeVisible (statusLabel);
    statusLabel.setJustificationType (juce::Justification::centredRight);
    statusLabel.setFont (juce::Font (juce::FontOptions (13.0f)));
    statusLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    statusLabel.setText ("Native integration is enabled for pages loaded here \xe2\x80\x94 "
                          "only load pages you trust.",
                          juce::dontSendNotification);

    webView.onPageFinished = [this] (const juce::String& url)
    {
        urlBar.setText (url, juce::dontSendNotification);
        processor.setSavedUrl (url);
    };

    addAndMakeVisible (webView);

    // Added after webView so it always renders on top, including when the
    // rest of the chrome is hidden and the web view covers the whole window.
    addAndMakeVisible (chromeToggleButton);
    chromeToggleButton.onClick = [this] { setChromeVisible (! chromeVisible); };

    const auto savedUrl = processor.getSavedUrl();
    loadUrl (savedUrl.isNotEmpty() ? savedUrl : defaultAppUrl);

    setResizable (true, true);
    setResizeLimits (480, 360, 3840, 2160);
    setSize (900, 650);

    startTimerHz (30);
}

DawBridgeAudioProcessorEditor::~DawBridgeAudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
void DawBridgeAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    // WebBrowserComponent is backed by a native windowed control on every
    // platform (WebKitGTK/WKWebView/WebView2), which always paints above
    // JUCE-drawn siblings regardless of child z-order. So the toggle button
    // can never be allowed to overlap webView's bounds -- it wouldn't be
    // clickable (or visible) if it did. This slim strip is reserved for it
    // whether or not the rest of the chrome is showing.
    auto toggleStrip = bounds.removeFromTop (22);
    chromeToggleButton.setBounds (toggleStrip.removeFromRight (90).reduced (2, 1));

    if (! chromeVisible)
    {
        webView.setBounds (bounds);
        return;
    }

    auto contentBounds = bounds.reduced (8, 4);
    auto topRow = contentBounds.removeFromTop (28);

    urlBarLabel.setBounds (topRow.removeFromLeft (40));
    goButton.setBounds (topRow.removeFromRight (60));
    topRow.removeFromRight (4);
    homeButton.setBounds (topRow.removeFromRight (70));
    topRow.removeFromRight (4);
    urlBar.setBounds (topRow);

    contentBounds.removeFromTop (4);
    statusLabel.setBounds (contentBounds.removeFromBottom (18));
    contentBounds.removeFromBottom (4);

    webView.setBounds (contentBounds);
}

//==============================================================================
void DawBridgeAudioProcessorEditor::setChromeVisible (bool shouldBeVisible)
{
    chromeVisible = shouldBeVisible;

    urlBarLabel.setVisible (chromeVisible);
    urlBar.setVisible (chromeVisible);
    goButton.setVisible (chromeVisible);
    homeButton.setVisible (chromeVisible);
    statusLabel.setVisible (chromeVisible);

    chromeToggleButton.setButtonText (chromeVisible ? "Maximize" : "Restore");

    resized();
}

//==============================================================================
void DawBridgeAudioProcessorEditor::loadUrl (const juce::String& url)
{
    if (url.isEmpty())
        return;

    urlBar.setText (url, juce::dontSendNotification);
    webView.goToURL (url);
    processor.setSavedUrl (url);
}

//==============================================================================
void DawBridgeAudioProcessorEditor::timerCallback()
{
    const auto snapshot = processor.getTransportSnapshot();

    juce::DynamicObject::Ptr transport = new juce::DynamicObject();
    transport->setProperty ("bpm", snapshot.bpm);
    transport->setProperty ("ppqPosition", snapshot.ppqPosition);
    transport->setProperty ("isPlaying", snapshot.isPlaying);
    transport->setProperty ("isRecording", snapshot.isRecording);
    transport->setProperty ("sampleRate", snapshot.sampleRate);

    webView.emitEventIfBrowserIsVisible ("hostTransport", juce::var (transport.get()));

    for (const auto& message : processor.drainHostMidiForWeb())
    {
        juce::Array<juce::var> bytes;

        for (int i = 0; i < message.getRawDataSize(); ++i)
            bytes.add ((int) message.getRawData()[i]);

        juce::DynamicObject::Ptr midiEvent = new juce::DynamicObject();
        midiEvent->setProperty ("bytes", bytes);

        webView.emitEventIfBrowserIsVisible ("hostMidi", juce::var (midiEvent.get()));
    }
}

//==============================================================================
void DawBridgeAudioProcessorEditor::handleWebAudioChunk (const juce::var& payload)
{
    auto* obj = payload.getDynamicObject();

    if (obj == nullptr)
        return;

    const int numChannels = (int) obj->getProperty ("numChannels");
    const int numFrames = (int) obj->getProperty ("numFrames");
    const auto base64 = obj->getProperty ("pcmBase64").toString();

    if (numChannels <= 0 || numFrames <= 0 || base64.isEmpty())
        return;

    juce::MemoryOutputStream decoded;

    if (! juce::Base64::convertFromBase64 (decoded, base64))
        return;

    const auto numFloatsAvailable = decoded.getDataSize() / sizeof (float);

    if ((int64_t) numFloatsAvailable < (int64_t) numFrames * (int64_t) numChannels)
        return;

    processor.pushWebAudio (static_cast<const float*> (decoded.getData()), numFrames, numChannels);
}

void DawBridgeAudioProcessorEditor::handleWebMidiOut (const juce::var& payload)
{
    auto* obj = payload.getDynamicObject();

    if (obj == nullptr)
        return;

    if (auto* bytesArray = obj->getProperty ("bytes").getArray())
    {
        std::vector<juce::uint8> bytes;
        bytes.reserve ((size_t) bytesArray->size());

        for (auto& v : *bytesArray)
            bytes.push_back ((juce::uint8) (int) v);

        if (! bytes.empty())
            processor.pushWebMidi (juce::MidiMessage (bytes.data(), (int) bytes.size()));
    }
}
