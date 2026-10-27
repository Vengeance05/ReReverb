cmake_minimum_required(VERSION 3.22)
project(Rereverb VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
if (APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if (MSVC)
    add_compile_options(/utf-8 /MP)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---------------------------------------------------------------------------
# Framework-independent DSP core shared by both plugins.
add_library(rr_core STATIC
    core/src/early_reflections.cpp
    core/src/late_reverb.cpp
    core/src/vista_engine.cpp
    core/src/ir_reshaper.cpp
    core/src/m7_library.cpp
    core/src/septima_engine.cpp
)
target_include_directories(rr_core PUBLIC core/include)
target_compile_features(rr_core PUBLIC cxx_std_20)
target_compile_options(rr_core PRIVATE
    $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic -O3>)

enable_testing()
add_executable(rr_tests tests/rr_tests.cpp)
target_link_libraries(rr_tests PRIVATE rr_core)
add_test(NAME rr_tests COMMAND rr_tests)

add_executable(rr_render tools/render.cpp)
target_link_libraries(rr_render PRIVATE rr_core)

# ---------------------------------------------------------------------------
# JUCE plugins (VST3 + AU on macOS, VST3 elsewhere).
option(RR_BUILD_PLUGINS "Build the Septima and Vista Rooms plugins" ON)
if (RR_BUILD_PLUGINS)
    set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dependencies/JUCE" CACHE PATH "JUCE source directory")
    if (NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "JUCE not found at ${JUCE_DIR}. Set -DJUCE_DIR=... or -DRR_BUILD_PLUGINS=OFF")
    endif()
    add_subdirectory("${JUCE_DIR}" JUCE)

    if (APPLE)
        set(RR_FORMATS VST3 AU)
    else()
        set(RR_FORMATS VST3 Standalone)
    endif()

    juce_add_binary_data(rr_fonts NAMESPACE RRFonts HEADER_NAME BinaryData.h SOURCES
        plugins/common/fonts/Inter-Regular.ttf
        plugins/common/fonts/Inter-Medium.ttf
        plugins/common/fonts/Inter-SemiBold.ttf)
    set_target_properties(rr_fonts PROPERTIES POSITION_INDEPENDENT_CODE ON)

    add_library(rr_plugin_common INTERFACE)
    target_sources(rr_plugin_common INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/plugins/common/LookAndFeel.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/plugins/common/Widgets.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/plugins/common/PresetManager.cpp)
    target_include_directories(rr_plugin_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/plugins/common)

    function(rr_add_plugin target product code)
        juce_add_plugin(${target}
            COMPANY_NAME "Rereverb"
            BUNDLE_ID "uk.vengeance.rereverb.${target}"
            IS_SYNTH FALSE
            NEEDS_MIDI_INPUT FALSE
            NEEDS_MIDI_OUTPUT FALSE
            COPY_PLUGIN_AFTER_BUILD ${RR_COPY_AFTER_BUILD}
            PLUGIN_MANUFACTURER_CODE Rrvb
            PLUGIN_CODE ${code}
            FORMATS ${RR_FORMATS}
            VST3_CATEGORIES Fx Reverb
            AU_MAIN_TYPE kAudioUnitType_Effect
            PRODUCT_NAME "${product}")
        target_link_libraries(${target} PRIVATE
            rr_core rr_plugin_common rr_fonts
            juce::juce_audio_utils juce::juce_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
        target_compile_definitions(${target} PRIVATE
            JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
            JUCE_DISPLAY_SPLASH_SCREEN=0)
    endfunction()

    option(RR_COPY_AFTER_BUILD "Install plugins into the user plugin folders after building" ON)

    rr_add_plugin(Septima "Septima" Sptm)
    target_sources(Septima PRIVATE
        plugins/septima/SeptimaProcessor.cpp
        plugins/septima/SeptimaEditor.cpp)

    rr_add_plugin(VistaRooms "Vista Rooms" Vsta)
    target_sources(VistaRooms PRIVATE
        plugins/vista_rooms/VistaProcessor.cpp
        plugins/vista_rooms/VistaEditor.cpp)
endif()
