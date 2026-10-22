cmake_minimum_required(VERSION 3.22)
# JUCE (and its bundled VST3/AU dependencies) contain C sources, so the C
# language must be enabled alongside C++. With CXX only, CMake generation on
# macOS failed with: "Missing variable is: CMAKE_C_COMPILE_OBJECT".
project(DARVC VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

set(DARVC_FORMATS VST3)
if(APPLE)
    list(APPEND DARVC_FORMATS AU)
endif()

juce_add_plugin(DARVC
    COMPANY_NAME "DARVC Audio"
    BUNDLE_ID "com.darvc.voiceconverter"
    # AU/GarageBand require both four-character codes to start with a single
    # upper-case letter followed by lower-case letters, so "DARV"/"DRVC"
    # (all upper-case) would fail AU validation on macOS hosts.
    PLUGIN_MANUFACTURER_CODE Darv
    PLUGIN_CODE Drvc
    PRODUCT_NAME "DARVC"
    DESCRIPTION "DARVC RVC Voice Converter"
    FORMATS ${DARVC_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(DARVC PRIVATE
    plugin/PluginProcessor.cpp
    plugin/PluginEditor.cpp
)

target_include_directories(DARVC PRIVATE plugin)

# The sources contain UTF-8 text (e.g. "•"). Without this flag MSVC reads
# them in the local ANSI code page and the strings show up garbled.
target_compile_options(DARVC PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_compile_definitions(DARVC PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(DARVC PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_extra
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags
)
