cmake_minimum_required(VERSION 3.22)
project(Histogram VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# Auto-install to the local plugin folder when building on your own machine,
# but not on a CI runner, where there is nothing to install into.
if(DEFINED ENV{CI})
    set(COPY_AFTER_BUILD FALSE)
else()
    set(COPY_AFTER_BUILD TRUE)
endif()

juce_add_plugin(Histogram
    COMPANY_NAME              "YourName"
    PLUGIN_MANUFACTURER_CODE  Ynam
    PLUGIN_CODE               Hst1
    FORMATS                   VST3 Standalone
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    COPY_PLUGIN_AFTER_BUILD   ${COPY_AFTER_BUILD}
    PRODUCT_NAME              "Histogram")

target_sources(Histogram PRIVATE PluginProcessor.cpp)

target_compile_definitions(Histogram PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Histogram
    PRIVATE juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
