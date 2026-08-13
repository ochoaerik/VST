#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

/** Access to the embedded JS bridge script. Compiled into the binary via
    juce_add_binary_data (see CMakeLists.txt) so the built plugin doesn't
    depend on files on disk.
*/
namespace DawBridgeResources
{
    /** The script injected into every page the browser navigates to (see
        WebBrowserComponent::Options::withUserScript). It hijacks
        AudioContext output so Web Audio can be captured, and installs a
        synthetic Web MIDI + host-transport bridge. */
    juce::String getBridgeScript();
}
