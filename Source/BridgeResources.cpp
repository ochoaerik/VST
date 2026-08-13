#include "BridgeResources.h"
#include "WebTapAssets.h"

#include <cstring>

namespace WebTapResources
{

juce::String getBridgeScript()
{
    return juce::String::fromUTF8 (WebTapAssets::bridge_js, WebTapAssets::bridge_jsSize);
}

std::optional<juce::WebBrowserComponent::Resource> getExampleResource (const juce::String& url)
{
    juce::ignoreUnused (url);

    std::vector<std::byte> bytes ((size_t) WebTapAssets::example_synth_htmlSize);
    std::memcpy (bytes.data(), WebTapAssets::example_synth_html, bytes.size());

    return juce::WebBrowserComponent::Resource { std::move (bytes), juce::String ("text/html") };
}

} // namespace WebTapResources
