cmake_minimum_required(VERSION 3.22)

project(WebTap VERSION 0.1.0)

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
    set(WEBTAP_FORMATS VST3 AU Standalone)
else()
    set(WEBTAP_FORMATS VST3 Standalone)
endif()

juce_add_plugin(WebTap
    COMPANY_NAME "WebTap"
    PLUGIN_MANUFACTURER_CODE Wtpm
    PLUGIN_CODE Wtp1
    FORMATS ${WEBTAP_FORMATS}
    PRODUCT_NAME "WebTap"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    NEEDS_WEB_BROWSER TRUE
    NEEDS_WEBVIEW2 TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_generate_juce_header(WebTap)

# The bridge script injected into every page the browser loads, and the
# bundled example instrument page, are embedded into the binary so the built
# plugin doesn't depend on the Resources folder being present at runtime.
juce_add_binary_data(WebTapAssets
    NAMESPACE WebTapAssets
    HEADER_NAME WebTapAssets.h
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/Resources/bridge.js
        ${CMAKE_CURRENT_SOURCE_DIR}/Resources/example_synth.html)

target_sources(WebTap
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/BridgeResources.cpp)

target_compile_definitions(WebTap
    PUBLIC
        JUCE_WEB_BROWSER=1
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_include_directories(WebTap
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source)

target_link_libraries(WebTap
    PRIVATE
        WebTapAssets
        juce::juce_audio_utils
        juce::juce_audio_plugin_client
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
