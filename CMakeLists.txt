cmake_minimum_required(VERSION 3.22)

# Single version source: the VERSION file ("0.2.0-beta.1" or "0.2.0"). project() and the JUCE plugin
# metadata take the numeric part; GROOVEMORPH_VERSION_STRING (groovemorph/Version.h) carries the full string.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" GM_VERSION_FULL LIMIT_COUNT 1)
string(STRIP "${GM_VERSION_FULL}" GM_VERSION_FULL)
if(NOT GM_VERSION_FULL MATCHES "^([0-9]+\\.[0-9]+\\.[0-9]+)(-beta\\.([0-9]+))?$")
    message(FATAL_ERROR "VERSION must be MAJOR.MINOR.PATCH or MAJOR.MINOR.PATCH-beta.N, got '${GM_VERSION_FULL}'")
endif()
set(GM_VERSION_NUMERIC "${CMAKE_MATCH_1}")
if(CMAKE_MATCH_3 STREQUAL "")
    set(GM_VERSION_PRERELEASE "")
else()
    set(GM_VERSION_PRERELEASE "beta.${CMAKE_MATCH_3}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")

project(GrooveMorph VERSION ${GM_VERSION_NUMERIC} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GROOVEMORPH_CORE_ONLY_BUILD "Build portable core and tests without fetching JUCE" OFF)
option(BUILD_TESTING "Build GrooveMorph tests" ON)
option(GROOVEMORPH_ENABLE_CLAP "Also build the CLAP format via clap-juce-extensions (ignored for core-only builds)" ON)

include(cmake/GrooveMorphPins.cmake)
configure_file(cmake/Version.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/groovemorph/Version.h" @ONLY)

# Portable core: every .cpp under src/core is part of the library.
file(GLOB_RECURSE GROOVEMORPH_CORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp)
add_library(GrooveMorphCore STATIC ${GROOVEMORPH_CORE_SOURCES})
target_include_directories(GrooveMorphCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_features(GrooveMorphCore PUBLIC cxx_std_20)
# TraceRing's concurrency test and any std::thread use in tests need the platform thread library.
find_package(Threads REQUIRED)
target_link_libraries(GrooveMorphCore PUBLIC Threads::Threads)
# Linked into the VST3 shared object, so the static core must be position independent.
set_target_properties(GrooveMorphCore PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(BUILD_TESTING)
    include(CTest)
    # Each tests/core/<Name>Tests.cpp is a standalone executable registered with CTest.
    file(GLOB GROOVEMORPH_CORE_TESTS CONFIGURE_DEPENDS tests/core/*Tests.cpp)
    foreach(test_source IN LISTS GROOVEMORPH_CORE_TESTS)
        get_filename_component(test_name ${test_source} NAME_WE)
        add_executable(${test_name} ${test_source})
        target_link_libraries(${test_name} PRIVATE GrooveMorphCore)
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endif()

if(NOT GROOVEMORPH_CORE_ONLY_BUILD)
    include(FetchContent)
    FetchContent_Declare(
        juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${GM_JUCE_GIT_TAG}
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(juce)

    if(GROOVEMORPH_ENABLE_CLAP)
        # Must be made available after JUCE. Pinned to a commit (see cmake/GrooveMorphPins.cmake); recursive
        # submodules (clap, clap-helpers) are fetched by default. No GIT_SHALLOW: a bare commit cannot be
        # shallow-cloned reliably.
        FetchContent_Declare(
            clap_juce_extensions
            GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
            GIT_TAG ${GM_CLAP_JUCE_EXT_GIT_TAG}
            GIT_SUBMODULES_RECURSE TRUE
        )
        FetchContent_MakeAvailable(clap_juce_extensions)
    endif()

    juce_add_plugin(GrooveMorphPlugin
        COMPANY_NAME "djshellshoxxx"
        PLUGIN_MANUFACTURER_CODE "Djsl"
        PLUGIN_CODE "Gm01"
        FORMATS VST3 Standalone
        PRODUCT_NAME "GrooveMorph"
        VERSION "${GM_VERSION_NUMERIC}"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    )

    target_sources(GrooveMorphPlugin PRIVATE
        src/PluginProcessor.cpp
        src/PluginEditor.cpp
        src/PresetManager.cpp
        src/ui/PresetBar.cpp
        src/PluginProcessorEasy.cpp
        src/ui/EasyView.cpp
        src/ui/FirstRunGuide.cpp
        src/ui/InspectorPanel.cpp
        src/ui/EuclidPanel.cpp
        src/PluginProcessorLoop.cpp
        src/ui/WaveformView.cpp
        src/ui/ControlMenu.cpp
        src/ui/PrintPanel.cpp
        src/ui/visual/VisualWindow.cpp
        src/ui/visual/WeaveView.cpp
        src/ui/visual/LedgerView.cpp
        src/ui/visual/Glyphs.cpp
        src/ui/visual/StationsView.cpp
        src/ui/visual/RingsView.cpp
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

    if(GROOVEMORPH_ENABLE_CLAP)
        # Creates the GrooveMorphPlugin_CLAP target (output: GrooveMorphPlugin_artefacts/<Config>/CLAP).
        clap_juce_extensions_plugin(TARGET GrooveMorphPlugin
            CLAP_ID "com.djshellshoxxx.groovemorph"
            CLAP_FEATURES audio-effect glitch stereo)
    endif()

    if(BUILD_TESTING)
        # Headless processor tests link the plugin's shared code target.
        add_executable(GrooveMorphPluginTests tests/PluginProcessorTests.cpp)
        target_include_directories(GrooveMorphPluginTests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            $<TARGET_PROPERTY:GrooveMorphPlugin,INCLUDE_DIRECTORIES>
        )
        target_compile_definitions(GrooveMorphPluginTests PRIVATE
            $<TARGET_PROPERTY:GrooveMorphPlugin,COMPILE_DEFINITIONS>
        )
        target_link_libraries(GrooveMorphPluginTests PRIVATE GrooveMorphPlugin)
        if(MSVC)
            # main() holds many processors/editors by value; MSVC does not share their stack slots,
            # so the default 1 MB stack can overflow on entry. Test executable only.
            target_link_options(GrooveMorphPluginTests PRIVATE /STACK:16777216)
        endif()
        add_test(NAME GrooveMorphPluginTests COMMAND GrooveMorphPluginTests)

        # W3: processor-level tests for sanitizing, smoothing, Dry return, morph, capacity, timer, evolution.
        add_executable(GrooveMorphW3ProcessorTests tests/W3ProcessorTests.cpp)
        target_include_directories(GrooveMorphW3ProcessorTests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            $<TARGET_PROPERTY:GrooveMorphPlugin,INCLUDE_DIRECTORIES>
        )
        target_compile_definitions(GrooveMorphW3ProcessorTests PRIVATE
            $<TARGET_PROPERTY:GrooveMorphPlugin,COMPILE_DEFINITIONS>
        )
        target_link_libraries(GrooveMorphW3ProcessorTests PRIVATE GrooveMorphPlugin)
        add_test(NAME GrooveMorphW3ProcessorTests COMMAND GrooveMorphW3ProcessorTests)

        # A4: offline audio smoke matrix (sample rates, block sizes, effects, chaos, loops, print, transport storms).
        add_executable(GrooveMorphAudioSmokeTests tests/AudioSmokeTests.cpp)
        target_include_directories(GrooveMorphAudioSmokeTests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            $<TARGET_PROPERTY:GrooveMorphPlugin,INCLUDE_DIRECTORIES>
        )
        target_compile_definitions(GrooveMorphAudioSmokeTests PRIVATE
            $<TARGET_PROPERTY:GrooveMorphPlugin,COMPILE_DEFINITIONS>
        )
        target_link_libraries(GrooveMorphAudioSmokeTests PRIVATE GrooveMorphPlugin)
        add_test(NAME GrooveMorphAudioSmokeTests COMMAND GrooveMorphAudioSmokeTests)

        # Developer tool (not a CTest): renders the real editor, software-rendered, to PNG files.
        add_executable(GrooveMorphEditorSnapshot tools/EditorSnapshot.cpp)
        target_include_directories(GrooveMorphEditorSnapshot PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            $<TARGET_PROPERTY:GrooveMorphPlugin,INCLUDE_DIRECTORIES>
        )
        target_compile_definitions(GrooveMorphEditorSnapshot PRIVATE
            $<TARGET_PROPERTY:GrooveMorphPlugin,COMPILE_DEFINITIONS>
        )
        target_link_libraries(GrooveMorphEditorSnapshot PRIVATE GrooveMorphPlugin)
    endif()
endif()
