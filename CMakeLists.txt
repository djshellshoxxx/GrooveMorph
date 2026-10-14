cmake_minimum_required(VERSION 3.22)

project(GrooveMorph VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GROOVEMORPH_CORE_ONLY_BUILD "Build portable core and tests without fetching JUCE" OFF)
option(BUILD_TESTING "Build GrooveMorph tests" ON)

# Portable core: every .cpp under src/core is part of the library.
file(GLOB_RECURSE GROOVEMORPH_CORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp)
add_library(GrooveMorphCore STATIC ${GROOVEMORPH_CORE_SOURCES})
target_include_directories(GrooveMorphCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
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
        src/ui/InspectorPanel.cpp
        src/ui/ControlMenu.cpp
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
