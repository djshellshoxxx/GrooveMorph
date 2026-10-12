cmake_minimum_required(VERSION 3.22)

project(GrooveMorph VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GROOVEMORPH_CORE_ONLY_BUILD "Build portable core and tests without fetching JUCE" OFF)
option(BUILD_TESTING "Build GrooveMorph tests" ON)

add_library(GrooveMorphCore STATIC
    src/core/PhraseClock.cpp
)
target_include_directories(GrooveMorphCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(GrooveMorphCore PUBLIC cxx_std_20)

if(BUILD_TESTING)
    include(CTest)
    add_executable(GrooveMorphCoreTests tests/PhraseClockTests.cpp)
    target_link_libraries(GrooveMorphCoreTests PRIVATE GrooveMorphCore)
    add_test(NAME GrooveMorphCoreTests COMMAND GrooveMorphCoreTests)
endif()

if(NOT GROOVEMORPH_CORE_ONLY_BUILD)
    include(FetchContent)
    FetchContent_Declare(
        juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.7
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(juce)

    juce_add_plugin(GrooveMorphPlugin
        COMPANY_NAME "djshellshoxxx"
        PLUGIN_MANUFACTURER_CODE "Djsl"
        PLUGIN_CODE "Gm01"
        FORMATS VST3 Standalone
        PRODUCT_NAME "GrooveMorph"
        VERSION "0.1.0"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    )

    target_sources(GrooveMorphPlugin PRIVATE
        src/PluginProcessor.cpp
        src/PluginEditor.cpp
    )
    target_include_directories(GrooveMorphPlugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(GrooveMorphPlugin PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
    )
    target_link_libraries(GrooveMorphPlugin PRIVATE
        GrooveMorphCore
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_extra
    )
    juce_generate_juce_header(GrooveMorphPlugin)
endif()
