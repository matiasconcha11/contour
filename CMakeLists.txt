cmake_minimum_required(VERSION 3.22)

# Must be set before project(). Builds are native-only by default; release builds pass
# -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" for a universal binary.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(Contour VERSION 0.3.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Contour
    COMPANY_NAME "Matias Concha"
    BUNDLE_ID com.matiasconcha.contour
    PLUGIN_MANUFACTURER_CODE Mcon
    PLUGIN_CODE Cntr
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Contour"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE)

set(CONTOUR_SOURCES
    Source/Components.cpp
    Source/Curve.cpp
    Source/CurveEditor.cpp
    Source/LookAndFeel.cpp
    Source/Parameters.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PresetManager.cpp)

target_sources(Contour PRIVATE ${CONTOUR_SOURCES})
target_compile_definitions(Contour PRIVATE CONTOUR_VERSION="${PROJECT_VERSION}")

target_compile_definitions(Contour PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Contour
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

option(CONTOUR_BUILD_TESTS "Build the unit tests" ON)

if(CONTOUR_BUILD_TESTS)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.16.1
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(Catch2)

    # The tests compile the same sources as the plugin, without a plugin wrapper around them.
    add_executable(ContourTests
        ${CONTOUR_SOURCES}
        Tests/CurveTests.cpp
        Tests/ModuleTests.cpp
        Tests/PresetTests.cpp
        Tests/ProcessorTests.cpp)

    target_link_libraries(ContourTests PRIVATE
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
        Catch2::Catch2WithMain)

    target_include_directories(ContourTests PRIVATE Source)
    target_compile_definitions(ContourTests PRIVATE
        CONTOUR_VERSION="${PROJECT_VERSION}"
        JUCE_STANDALONE_APPLICATION=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

    include(CTest)
    include(${catch2_SOURCE_DIR}/extras/Catch.cmake)
    catch_discover_tests(ContourTests)
endif()
