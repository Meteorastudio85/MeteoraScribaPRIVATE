cmake_minimum_required(VERSION 3.22)
project(Scriba VERSION 0.6.2)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# --- whisper.cpp: lyrics transcription (VOCALS mode). Static, no extra DLLs next to the plugin. ---
set(CMAKE_POSITION_INDEPENDENT_CODE ON)        # static libs linked into a shared plugin
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(WHISPER_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(GGML_NATIVE OFF CACHE BOOL "" FORCE)       # portable: runs on any CPU with AVX2 (2013+), not only on the build machine
set(GGML_OPENMP OFF CACHE BOOL "" FORCE)       # no OpenMP runtime to ship (ggml uses its own thread pool)
if(APPLE)
    set(GGML_METAL ON CACHE BOOL "" FORCE)     # GPU on Apple Silicon
    set(GGML_METAL_EMBED_LIBRARY ON CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(whisper
    GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
    GIT_TAG v1.8.2
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(whisper)

# --- RTNeural (BSD-3): runs the Basic Pitch network for GUITAR / PIANO. Eigen backend, header only. ---
FetchContent_Declare(RTNeural
    GIT_REPOSITORY https://github.com/jatinchowdhury18/RTNeural.git
    GIT_TAG 2ca066e5e529ea3ed4120c368ee16526eeaf2cec
    SOURCE_SUBDIR headers-only)       # only the headers are used: its own CMake project is not added
FetchContent_MakeAvailable(RTNeural)

# Linux: JUCE needs libcurl to download the voice model (macOS and Windows use the system networking)
option(SCRIBA_USE_CURL "Linux: use libcurl for the model download" ON)

option(SCRIBA_COPY_AFTER_BUILD "Copia il plugin nelle cartelle di sistema dopo la build" OFF)

if(APPLE)
    set(SCRIBA_FORMATS VST3 AU Standalone)
else()
    set(SCRIBA_FORMATS VST3 Standalone)
endif()

juce_add_plugin(Scriba
    COMPANY_NAME "Meteora Plugins"
    COMPANY_COPYRIGHT "Copyright (c) 2026 Meteora Plugins. All rights reserved."
    COMPANY_WEBSITE "https://meteoraplugins.gumroad.com"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    PLUGIN_MANUFACTURER_CODE Yrco
    PLUGIN_CODE Scrb
    FORMATS ${SCRIBA_FORMATS}
    PRODUCT_NAME "Scriba"
    BUNDLE_ID com.meteoraplugins.scriba
    NEEDS_CURL ${SCRIBA_USE_CURL}
    COPY_PLUGIN_AFTER_BUILD ${SCRIBA_COPY_AFTER_BUILD})

juce_add_binary_data(ScribaAssets SOURCES
    Resources/logo.png
    Resources/ScribaSans-Regular.ttf
    Resources/ScribaSans-Bold.ttf
    Resources/basicpitch/cnn_contour_model.json
    Resources/basicpitch/cnn_note_model.json
    Resources/basicpitch/cnn_onset_1_model.json
    Resources/basicpitch/cnn_onset_2_model.json)

target_sources(Scriba PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/LyricsTranscriber.cpp
    Source/basicpitch/BasicPitchFeatures.cpp
    Source/basicpitch/BasicPitchCNN.cpp
    Source/basicpitch/Notes.cpp
    Source/basicpitch/BasicPitchTranscriber.cpp)

target_include_directories(Scriba PRIVATE
    Source
    Source/basicpitch
    ${rtneural_SOURCE_DIR}
    ${rtneural_SOURCE_DIR}/modules/Eigen
    ${rtneural_SOURCE_DIR}/modules/json)

target_compile_options(Scriba PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_compile_definitions(Scriba PUBLIC
    RTNEURAL_USE_EIGEN=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=$<IF:$<BOOL:${SCRIBA_USE_CURL}>,1,0>
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Scriba
    PRIVATE
        ScribaAssets
        whisper
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
