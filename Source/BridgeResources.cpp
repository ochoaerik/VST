#include "BridgeResources.h"
#include "DawBridgeAssets.h"

#include <cstring>

namespace DawBridgeResources
{

juce::String getBridgeScript()
{
    return juce::String::fromUTF8 (DawBridgeAssets::bridge_js, DawBridgeAssets::bridge_jsSize);
}

std::optional<juce::WebBrowserComponent::Resource> getExampleResource (const juce::String& url)
{
    juce::ignoreUnused (url);

    std::vector<std::byte> bytes ((size_t) DawBridgeAssets::example_synth_htmlSize);
    std::memcpy (bytes.data(), DawBridgeAssets::example_synth_html, bytes.size());

    return juce::WebBrowserComponent::Resource { std::move (bytes), juce::String ("text/html") };
}

} // namespace DawBridgeResources
