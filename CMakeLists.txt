cmake_minimum_required (VERSION 3.22)

project (HAIRMONY3000 VERSION 1.0.0 LANGUAGES C CXX)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set_property (GLOBAL PROPERTY USE_FOLDERS ON)

option (HAIRMONY_BUILD_TESTS "Build the engine and plugin test executables" ON)

# Static MSVC runtime so the VST3 has no Visual C++ redistributable dependency.
if (MSVC)
    set (CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

#==============================================================================
# JUCE (fetched at configure time unless JUCE_DIR points at a local checkout)
include (FetchContent)
set (HAIRMONY_JUCE_TAG "8.0.10" CACHE STRING "JUCE git tag to build against")
if (DEFINED JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory ("${JUCE_DIR}" JUCE EXCLUDE_FROM_ALL)
else()
    FetchContent_Declare (JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${HAIRMONY_JUCE_TAG}
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE)
    FetchContent_MakeAvailable (JUCE)
endif()

#==============================================================================
# Engine: plain C++, no JUCE, so it can be unit tested on its own.
add_library (hairmony_core STATIC
    Source/core/Fft.cpp
    Source/core/DspUtils.cpp
    Source/core/PitchTracker.cpp
    Source/core/ChoirEngine.cpp)
target_include_directories (hairmony_core PUBLIC Source/core)
find_package (Threads REQUIRED)
target_link_libraries (hairmony_core PUBLIC Threads::Threads)
set_target_properties (hairmony_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if (MSVC)
    target_compile_options (hairmony_core PRIVATE /W4 /fp:fast /O2)
    target_compile_definitions (hairmony_core PRIVATE _USE_MATH_DEFINES NOMINMAX)
else()
    target_compile_options (hairmony_core PRIVATE -Wall -Wextra -O2)
endif()

#==============================================================================
# Plugin
set (HAIRMONY_FORMATS VST3 Standalone)

juce_add_plugin (Hairmony
    PRODUCT_NAME "HAIRMONY 3000"
    COMPANY_NAME "Hairmony Labs"
    COMPANY_WEBSITE "https://github.com/"
    BUNDLE_ID "com.hairmonylabs.hairmony3000"
    PLUGIN_MANUFACTURER_CODE Hmlb
    PLUGIN_CODE Hm3k
    DESCRIPTION "Choir generator that sings your vocal back as gospel, kids, gang and cyborg choirs"
    VST3_CATEGORIES Fx Vocals
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS ${HAIRMONY_FORMATS})

target_sources (Hairmony PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Session.cpp
    Source/ui/MetalLookAndFeel.cpp
    Source/ui/Screens.cpp)

target_include_directories (Hairmony PRIVATE Source)

target_compile_definitions (Hairmony
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_USE_FLAC=1
        JUCE_USE_OGGVORBIS=1
        JUCE_MODAL_LOOPS_PERMITTED=1)

target_link_libraries (Hairmony
    PRIVATE
        hairmony_core
        juce::juce_audio_utils
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

#==============================================================================
if (HAIRMONY_BUILD_TESTS)
    enable_testing()

    add_executable (EngineTests tests/EngineTests.cpp)
    target_link_libraries (EngineTests PRIVATE hairmony_core)
    if (MSVC)
        target_compile_definitions (EngineTests PRIVATE _USE_MATH_DEFINES NOMINMAX)
    endif()
    add_test (NAME EngineTests COMMAND EngineTests)

    # Command-line renderer for listening tests: RenderTool in.f32 48000 outPrefix [choir] [mic] [flags]
    add_executable (RenderTool tests/RenderTool.cpp)
    target_link_libraries (RenderTool PRIVATE hairmony_core)
    if (MSVC)
        target_compile_definitions (RenderTool PRIVATE _USE_MATH_DEFINES NOMINMAX)
    endif()

    # Drives the real processor + editor: import, render, playback, state, export, UI flows.
    # The plugin sources are compiled straight into this app (rather than linking the plugin's
    # shared-code library) so it gets its own clean copy of the JUCE modules.
    juce_add_console_app (PluginTests PRODUCT_NAME "HairmonyPluginTests")
    target_sources (PluginTests PRIVATE
        tests/PluginTests.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/Session.cpp
        Source/ui/MetalLookAndFeel.cpp
        Source/ui/Screens.cpp)
    target_include_directories (PluginTests PRIVATE Source)
    target_compile_definitions (PluginTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_USE_FLAC=1
        JUCE_USE_OGGVORBIS=1
        JUCE_MODAL_LOOPS_PERMITTED=1
        HAIRMONY_TEST_BUILD=1)
    target_link_libraries (PluginTests PRIVATE
        hairmony_core
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test (NAME PluginTests COMMAND PluginTests)
endif()
