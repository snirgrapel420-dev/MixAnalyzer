cmake_minimum_required(VERSION 3.22)
project(ProductionAnalyzer VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
endif()

# JUCE 8 is required: it brings Unicode shaping + right-to-left (Hebrew) text layout.
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(ProductionAnalyzer
    COMPANY_NAME "Dali Audio"
    PRODUCT_NAME                "Mix Analyzer"
    PLUGIN_MANUFACTURER_CODE    Ystd
    PLUGIN_CODE                 PrAn
    FORMATS                     VST3 AU Standalone
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     TRUE
    VST3_CATEGORIES             Fx Analyzer
    AU_MAIN_TYPE                kAudioUnitType_Effect)

target_sources(ProductionAnalyzer PRIVATE
    Source/AnalysisEngine.cpp
    Source/ReportBuilder.cpp
    Source/AnalysisService.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(ProductionAnalyzer PRIVATE Source)

target_compile_definitions(ProductionAnalyzer PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1)

# Hebrew string literals are UTF-8 in the source files
if(MSVC)
    target_compile_options(ProductionAnalyzer PRIVATE /utf-8)
endif()

target_link_libraries(ProductionAnalyzer
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- Optional: engine unit test (no JUCE needed) --------------------------------
option(PA_BUILD_TESTS "Build the analysis engine test" OFF)
if(PA_BUILD_TESTS)
    add_executable(engine_test tests/engine_test.cpp Source/AnalysisEngine.cpp Source/ReportBuilder.cpp)
    target_include_directories(engine_test PRIVATE Source)
endif()
