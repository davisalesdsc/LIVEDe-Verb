cmake_minimum_required(VERSION 3.22)

# Universal binary (Apple Silicon + Intel). Must be set before project().
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

project(AuroraDeVerb VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

option(AURORA_COPY_AFTER_BUILD "Install plugins into the user plugin folders after building" OFF)
set(AURORA_VST2_SDK_PATH "" CACHE PATH "Optional: path to the legacy Steinberg VST2 SDK (enables the VST2 format)")

set(AURORA_FORMATS AU VST3 Standalone)
if(AURORA_VST2_SDK_PATH)
    juce_set_vst2_sdk_path("${AURORA_VST2_SDK_PATH}")
    list(APPEND AURORA_FORMATS VST)
endif()

juce_add_plugin(AuroraDeVerb
    COMPANY_NAME                "Aurora Audio"
    BUNDLE_ID                   com.auroraaudio.deverb
    PLUGIN_MANUFACTURER_CODE    Auro
    PLUGIN_CODE                 Advb
    PRODUCT_NAME                "Aurora DeVerb"
    DESCRIPTION                 "De-reverb for instrument recordings"
    FORMATS                     ${AURORA_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE                kAudioUnitType_Effect
    VST3_CATEGORIES             Fx Restoration
    VST2_CATEGORY               kPlugCategEffect
    HARDENED_RUNTIME_ENABLED    TRUE
    COPY_PLUGIN_AFTER_BUILD     ${AURORA_COPY_AFTER_BUILD})

target_sources(AuroraDeVerb PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(AuroraDeVerb PRIVATE Source)

target_compile_definitions(AuroraDeVerb PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(AuroraDeVerb
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# DSP unit tests (no JUCE needed)
enable_testing()
add_executable(AuroraDeVerbEngineTests tests/test_engine.cpp)
target_include_directories(AuroraDeVerbEngineTests PRIVATE Source)
add_test(NAME engine COMMAND AuroraDeVerbEngineTests)

# GUI snapshot tool (CI renders the editor to a PNG)
option(AURORA_BUILD_SNAPSHOT "Build the editor snapshot tool" OFF)
if(AURORA_BUILD_SNAPSHOT)
    juce_add_console_app(AuroraDeVerbSnapshot PRODUCT_NAME "AuroraDeVerbSnapshot")
    target_sources(AuroraDeVerbSnapshot PRIVATE
        tools/snapshot.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)
    target_include_directories(AuroraDeVerbSnapshot PRIVATE Source)
    target_compile_definitions(AuroraDeVerbSnapshot PRIVATE
        JucePlugin_Name="AuroraDeVerb"
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(AuroraDeVerbSnapshot
        PRIVATE juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags)
endif()
