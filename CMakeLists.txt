cmake_minimum_required(VERSION 3.22)

project(GrooveMorph VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.7
)

FetchContent_MakeAvailable(juce)

# VST3 plugin target
juce_add_plugin(GrooveMorphPlugin
    COMPANY_NAME "djshellshoxxx"
    PLUGIN_MANUFACTURER_CODE "Djsl"
    PLUGIN_CODE "Gm01"
    FORMATS VST3
    PRODUCT_NAME "GrooveMorph"
    VERSION "0.1.0"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
)

target_sources(GrooveMorphPlugin
    PRIVATE
        src/PluginProcessor.cpp
        src/PluginEditor.cpp
)

target_compile_definitions(GrooveMorphPlugin
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(GrooveMorphPlugin
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_extra
        juce::juce_gui_basics
        juce::juce_graphics
)

target_include_directories(GrooveMorphPlugin
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

juce_generate_juce_header(GrooveMorphPlugin)

# Standalone application target
juce_add_gui_app(GrooveMorphStandalone
    PRODUCT_NAME "GrooveMorph"
    VERSION "0.1.0"
)

target_sources(GrooveMorphStandalone
    PRIVATE
        src/StandaloneMain.cpp
        src/PluginProcessor.cpp
        src/PluginEditor.cpp
)

target_compile_definitions(GrooveMorphStandalone
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(GrooveMorphStandalone
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_gui_extra
        juce::juce_gui_basics
        juce::juce_graphics
        juce::juce_core
)

target_include_directories(GrooveMorphStandalone
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

juce_generate_juce_header(GrooveMorphStandalone)
