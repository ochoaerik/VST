cmake_minimum_required(VERSION 3.22)

project(DawBridge VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

include(FetchContent)

# JUCE provides the plugin wrapper (VST3/AU), the embedded browser
# (WebBrowserComponent) and the real-time audio/MIDI plumbing.
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

if(APPLE)
    set(DAWBRIDGE_FORMATS VST3 AU Standalone)
else()
    set(DAWBRIDGE_FORMATS VST3 Standalone)
endif()

juce_add_plugin(DawBridge
    COMPANY_NAME "DawBridge"
    PLUGIN_MANUFACTURER_CODE Dwbr
    PLUGIN_CODE Dwb1
    FORMATS ${DAWBRIDGE_FORMATS}
    PRODUCT_NAME "DawBridge"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    NEEDS_WEB_BROWSER TRUE
    NEEDS_WEBVIEW2 TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_generate_juce_header(DawBridge)

# The bridge script injected into every page the browser loads is embedded
# into the binary so the built plugin doesn't depend on the Resources folder
# being present at runtime.
juce_add_binary_data(DawBridgeAssets
    NAMESPACE DawBridgeAssets
    HEADER_NAME DawBridgeAssets.h
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/Resources/bridge.js)

target_sources(DawBridge
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/BridgeResources.cpp)

target_compile_definitions(DawBridge
    PUBLIC
        JUCE_WEB_BROWSER=1
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        # NEEDS_WEBVIEW2 (above) only makes CMake find/link the WebView2 SDK;
        # this is the separate compile-time flag that actually switches
        # WebBrowserComponent to the WebView2 backend on Windows (statically
        # linked, so no WebView2Loader.dll needs to ship alongside the exe).
        # A no-op on non-Windows platforms.
        JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1)

target_include_directories(DawBridge
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source)

target_link_libraries(DawBridge
    PRIVATE
        DawBridgeAssets
        juce::juce_audio_utils
        juce::juce_audio_plugin_client
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
