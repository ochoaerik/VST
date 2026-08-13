#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "BridgeResources.h"
#include "PluginProcessor.h"

/** WebBrowserComponent subclass that reports navigation completion so the
    editor can keep the URL bar and the plugin's persisted state in sync,
    even when the user follows a link inside the loaded page. */
class BridgedWebView : public juce::WebBrowserComponent
{
public:
    using juce::WebBrowserComponent::WebBrowserComponent;

    std::function<void (const juce::String&)> onPageFinished;

private:
    void pageFinishedLoading (const juce::String& url) override
    {
        if (onPageFinished != nullptr)
            onPageFinished (url);
    }
};

//==============================================================================
class DawBridgeAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    private juce::Timer
{
public:
    explicit DawBridgeAudioProcessorEditor (DawBridgeAudioProcessor&);
    ~DawBridgeAudioProcessorEditor() override;

    void resized() override;

private:
    void timerCallback() override;
    void loadUrl (const juce::String& url);
    void setChromeVisible (bool shouldBeVisible);

    void handleWebAudioChunk (const juce::var& payload);
    void handleWebMidiOut (const juce::var& payload);

    DawBridgeAudioProcessor& processor;

    bool chromeVisible = true;

    juce::Label urlBarLabel { {}, "Page:" };
    juce::TextEditor urlBar;
    juce::TextButton goButton { "Go" };
    juce::TextButton homeButton { "Home" };
    juce::TextButton chromeToggleButton { "Maximize" };
    juce::Label statusLabel;

    BridgedWebView webView
    {
        juce::WebBrowserComponent::Options {}
       #if JUCE_WINDOWS
            .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
            .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2 {}
                .withUserDataFolder (juce::File::getSpecialLocation (juce::File::SpecialLocationType::tempDirectory)))
       #endif
            .withNativeIntegrationEnabled()
            .withEventListener ("webAudioChunk", [this] (juce::var payload) { handleWebAudioChunk (payload); })
            .withEventListener ("webMidiOut", [this] (juce::var payload) { handleWebMidiOut (payload); })
            .withUserScript (DawBridgeResources::getBridgeScript())
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DawBridgeAudioProcessorEditor)
};
