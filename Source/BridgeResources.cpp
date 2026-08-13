#include "BridgeResources.h"
#include "DawBridgeAssets.h"

namespace DawBridgeResources
{

juce::String getBridgeScript()
{
    return juce::String::fromUTF8 (DawBridgeAssets::bridge_js, DawBridgeAssets::bridge_jsSize);
}

} // namespace DawBridgeResources
