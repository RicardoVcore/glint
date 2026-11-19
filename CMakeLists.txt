cmake_minimum_required (VERSION 3.22)

project (Glint VERSION 0.1.0 LANGUAGES C CXX)

set (CMAKE_CXX_STANDARD 20)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (CMAKE_CXX_EXTENSIONS OFF)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set (CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include (FetchContent)

FetchContent_Declare (JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.17-security
    GIT_SHALLOW    TRUE)

FetchContent_MakeAvailable (JUCE)

juce_add_plugin (Glint
    COMPANY_NAME              "Glint Audio"
    DESCRIPTION               "Variable-mu tube compressor"
    PLUGIN_MANUFACTURER_CODE  Glap
    PLUGIN_CODE               Glnt
    FORMATS                   VST3 Standalone
    PRODUCT_NAME              "Glint"
    BUNDLE_ID                 com.glintaudio.glint
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    COPY_PLUGIN_AFTER_BUILD   FALSE)

target_sources (Glint PRIVATE
    src/GlintDsp.cpp
    src/PluginProcessor.cpp)

target_compile_definitions (Glint PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries (Glint PUBLIC
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Warnings for our translation units only (JUCE module sources are untouched).
target_compile_options (Glint PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra>)

add_executable (glint_tests tests/GlintTests.cpp)

target_link_libraries (glint_tests PRIVATE Glint)

target_include_directories (glint_tests PRIVATE src)

target_compile_options (glint_tests PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra>)

enable_testing ()

add_test (NAME glint_tests COMMAND glint_tests)
