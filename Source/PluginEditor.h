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
    void updateRestoreOverlayVisibility();

    void handleWebAudioChunk (const juce::var& payload);
    void handleWebMidiOut (const juce::var& payload);

    DawBridgeAudioProcessor& processor;

    bool chromeVisible = true;

    juce::Label urlBarLabel { {}, "Page:" };
    juce::TextEditor urlBar;
    juce::TextButton goButton { "Go" };
    juce::TextButton homeButton { "Home" };
    juce::TextButton maximizeButton { "Maximize" };
    juce::Label statusLabel;

    /** A small always-on-top, borderless top-level window holding just the
        Restore button. Shown only while the mouse hovers near the top edge
        of the editor when maximized.

        This has to be a *separate OS window* rather than an ordinary child
        component: WebBrowserComponent is backed by a native windowed
        control on every platform (WebKitGTK/WKWebView/WebView2), and native
        child windows always paint above JUCE-drawn siblings in the same
        parent regardless of component z-order. When maximized, webView
        covers the entire editor, so nothing added as a normal child of this
        editor could ever be layered visibly on top of it. A distinct
        always-on-top desktop window sidesteps that by operating at the OS
        window-manager level instead. */
    struct RestoreOverlay : public juce::Component
    {
        RestoreOverlay()
        {
            addAndMakeVisible (restoreButton);
            setSize (90, 26);
            setAlwaysOnTop (true);
            setOpaque (true);
        }

        void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black); }
        void resized() override { restoreButton.setBounds (getLocalBounds()); }

        juce::TextButton restoreButton { "Restore" };
    };

    RestoreOverlay restoreOverlay;
    bool restoreOverlayShown = false;

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
