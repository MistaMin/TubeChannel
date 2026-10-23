cmake_minimum_required(VERSION 3.22)

# Single source of truth for the plugin version: VERSION (MAJOR.MINOR.PATCH).
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" TUBECHANNEL_VERSION_STR)
string(STRIP "${TUBECHANNEL_VERSION_STR}" TUBECHANNEL_VERSION_STR)
# Release builds for macOS: -DTUBECHANNEL_UNIVERSAL=ON gives an arm64 + x86_64 binary. Off by default
# so day-to-day builds compile once for the host.
option(TUBECHANNEL_UNIVERSAL "Build arm64 + x86_64 on macOS" OFF)
if(APPLE AND TUBECHANNEL_UNIVERSAL)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(TubeChannel VERSION ${TUBECHANNEL_VERSION_STR} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

# --- Engine test: no JUCE, runs on every platform ------------------------------------------------
add_executable(TubeChannelOversamplerTest tests/oversampler_test.cpp)
target_include_directories(TubeChannelOversamplerTest PRIVATE Source)
add_test(NAME oversampler COMMAND TubeChannelOversamplerTest)

add_executable(TubeChannelSparseLUTest tests/sparse_lu_test.cpp)
target_include_directories(TubeChannelSparseLUTest PRIVATE Source)
add_test(NAME sparse_lu COMMAND TubeChannelSparseLUTest)

add_executable(TubeChannelCircuitTest tests/circuit_test.cpp)
target_include_directories(TubeChannelCircuitTest PRIVATE Source)
add_test(NAME circuit COMMAND TubeChannelCircuitTest)

add_executable(TubeChannelSidechainTest tests/sidechain_test.cpp)
target_include_directories(TubeChannelSidechainTest PRIVATE Source)
add_test(NAME sidechain COMMAND TubeChannelSidechainTest ${CMAKE_CURRENT_SOURCE_DIR}/Netlists)

add_executable(TubeChannelLimiterTest tests/limiter_test.cpp)
target_include_directories(TubeChannelLimiterTest PRIVATE Source)
add_test(NAME limiter COMMAND TubeChannelLimiterTest ${CMAKE_CURRENT_SOURCE_DIR}/Netlists)

add_executable(TubeChannelLockstepTest tests/lockstep_test.cpp)
target_include_directories(TubeChannelLockstepTest PRIVATE Source)
add_test(NAME lockstep COMMAND TubeChannelLockstepTest ${CMAKE_CURRENT_SOURCE_DIR}/Netlists)

add_executable(TubeChannelRealtimeTest tests/realtime_test.cpp)
target_include_directories(TubeChannelRealtimeTest PRIVATE Source)
add_test(NAME realtime COMMAND TubeChannelRealtimeTest ${CMAKE_CURRENT_SOURCE_DIR}/Netlists)

add_executable(TubeChannelDetectorGuardTest tests/detector_guard_test.cpp)
target_include_directories(TubeChannelDetectorGuardTest PRIVATE Source)
add_test(NAME detector_guard COMMAND TubeChannelDetectorGuardTest ${CMAKE_CURRENT_SOURCE_DIR}/Netlists)

add_executable(TubeChannelQualityTest tests/quality_mode_test.cpp)
target_include_directories(TubeChannelQualityTest PRIVATE Source)
add_test(NAME quality_modes COMMAND TubeChannelQualityTest ${CMAKE_CURRENT_SOURCE_DIR}/Netlists)

add_executable(TubeChannelChainBench tests/chain_bench.cpp)
target_include_directories(TubeChannelChainBench PRIVATE Source)

add_executable(TubeChannelChainProfile tests/chain_bench.cpp)
target_include_directories(TubeChannelChainProfile PRIVATE Source)
target_compile_definitions(TubeChannelChainProfile PRIVATE TC_PROFILE=1)

add_executable(TubeChannelLinearLayoutTest tests/linear_layout_test.cpp)
target_include_directories(TubeChannelLinearLayoutTest PRIVATE Source)
add_test(NAME linear_layout COMMAND TubeChannelLinearLayoutTest ${CMAKE_CURRENT_SOURCE_DIR}/Netlists)

add_executable(TubeChannelTriodeLookupTest tests/triode_lookup_test.cpp)
target_include_directories(TubeChannelTriodeLookupTest PRIVATE Source)
add_test(NAME triode_lookup COMMAND TubeChannelTriodeLookupTest)

add_executable(TubeChannelReferenceWaveCapture tests/reference_wave_capture.cpp)
target_include_directories(TubeChannelReferenceWaveCapture PRIVATE Source)

add_executable(TubeChannelTubeCompare tests/tube_compare.cpp)
target_include_directories(TubeChannelTubeCompare PRIVATE Source)
target_compile_definitions(TubeChannelTubeCompare PRIVATE TC_TUBE_EXPERIMENT=1)
# Diagnostic fidelity gate: currently rejected at high drive, so not a normal plugin test.
add_executable(TubeChannelTubeLookupBench tests/chain_bench.cpp)
target_include_directories(TubeChannelTubeLookupBench PRIVATE Source)
target_compile_definitions(TubeChannelTubeLookupBench PRIVATE TC_TUBE_EXPERIMENT=1)
add_executable(TubeChannelTubeLookupProfile tests/chain_bench.cpp)
target_include_directories(TubeChannelTubeLookupProfile PRIVATE Source)
target_compile_definitions(TubeChannelTubeLookupProfile PRIVATE TC_TUBE_EXPERIMENT=1 TC_PROFILE=1)

add_executable(TubeChannelTubeReductionCapture tests/tube_reduction_capture.cpp)
target_include_directories(TubeChannelTubeReductionCapture PRIVATE Source)
target_compile_definitions(TubeChannelTubeReductionCapture PRIVATE TC_DETECTOR_EXPERIMENT=1)

add_executable(TubeChannelHybridModulesTest tests/hybrid_modules_test.cpp)
target_include_directories(TubeChannelHybridModulesTest PRIVATE Source)
add_test(NAME hybrid_modules COMMAND TubeChannelHybridModulesTest)

add_executable(TubeChannelHybridLinearValidate tests/hybrid_linear_validate.cpp)
target_include_directories(TubeChannelHybridLinearValidate PRIVATE Source)
add_test(NAME hybrid_linear COMMAND TubeChannelHybridLinearValidate ${CMAKE_CURRENT_SOURCE_DIR}/Netlists)

add_executable(TubeChannelHybridLinearCapture tests/hybrid_linear_capture.cpp)
target_include_directories(TubeChannelHybridLinearCapture PRIVATE Source)

add_executable(TubeChannelHybridSupplyCapture tests/hybrid_supply_capture.cpp)
target_include_directories(TubeChannelHybridSupplyCapture PRIVATE Source)

add_executable(TubeChannelHybridChainCompare tests/hybrid_chain_compare.cpp)
target_include_directories(TubeChannelHybridChainCompare PRIVATE Source)

add_executable(TubeChannelHybridCalibrate tests/hybrid_calibrate.cpp)
target_include_directories(TubeChannelHybridCalibrate PRIVATE Source)

add_executable(TubeChannelHybridDetectorCapture tests/hybrid_detector_capture.cpp)
target_include_directories(TubeChannelHybridDetectorCapture PRIVATE Source)

add_executable(TubeChannelDetectorCapture tests/detector_capture.cpp)
target_include_directories(TubeChannelDetectorCapture PRIVATE Source)
target_compile_definitions(TubeChannelDetectorCapture PRIVATE TC_DETECTOR_EXPERIMENT=1)

# Eco measurements and regression tools use the retained circuit as an offline reference.
add_executable(TubeChannelEcoTest tests/eco_test.cpp)
target_include_directories(TubeChannelEcoTest PRIVATE Source)
add_test(NAME eco COMMAND TubeChannelEcoTest)

add_executable(TubeChannelEcoListen tests/eco_listen.cpp)
target_include_directories(TubeChannelEcoListen PRIVATE Source)

add_executable(TubeChannelEcoCompare tests/eco_compare.cpp)
target_include_directories(TubeChannelEcoCompare PRIVATE Source)

add_executable(TubeChannelEcoCapture tests/eco_capture.cpp)
target_include_directories(TubeChannelEcoCapture PRIVATE Source)

# --- JUCE ------------------------------------------------------------------------------------------
option(TUBECHANNEL_BUILD_PLUGIN "Build the JUCE plugin targets" ON)
option(TUBECHANNEL_EXPERIMENTAL_SUPPLY "Use the unfitted compact supply in comparison builds" OFF)
if(NOT TUBECHANNEL_BUILD_PLUGIN)
    return()
endif()

include(FetchContent)
set(TUBECHANNEL_JUCE_SOURCE "" CACHE PATH "Use an existing JUCE checkout instead of downloading")
if(TUBECHANNEL_JUCE_SOURCE)
    set(FETCHCONTENT_SOURCE_DIR_JUCE "${TUBECHANNEL_JUCE_SOURCE}")
endif()
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.3
)
FetchContent_MakeAvailable(JUCE)

# --- GoodLookinUI (plugin-free toolkit, consumed like HybridEQ does) ---------------------------------
set(GOODLOOKINUI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/GoodLookinUI" CACHE PATH "GoodLookinUI toolkit location")
if(NOT EXISTS "${GOODLOOKINUI_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR
        "GoodLookinUI not found at ${GOODLOOKINUI_ROOT}.\n"
        "Add it as a submodule (git submodule add https://github.com/MistaMin/GoodLookinUI.git third_party/GoodLookinUI)\n"
        "or pass -DGOODLOOKINUI_ROOT=<path to a checkout>.")
endif()
add_subdirectory("${GOODLOOKINUI_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/goodlookinui")

# --- Formats -----------------------------------------------------------------------------------------
# VST3 + Standalone everywhere, AU on macOS, LV2 everywhere (the native format on Linux). CLAP is
# added below through clap-juce-extensions.
set(TUBECHANNEL_FORMATS VST3 Standalone LV2)
set(TUBECHANNEL_EXTRA_ARGS LV2URI "urn:opengrid:tubechannel")
if(APPLE)
    list(APPEND TUBECHANNEL_FORMATS AU)
endif()
set(TUBECHANNEL_AAX_SDK "${CMAKE_SOURCE_DIR}/../aax-sdk-2-8-1" CACHE PATH "Optional AAX SDK")
if((APPLE OR WIN32) AND EXISTS "${TUBECHANNEL_AAX_SDK}/Interfaces/AAX.h")
    juce_set_aax_sdk_path("${TUBECHANNEL_AAX_SDK}")
    list(APPEND TUBECHANNEL_FORMATS AAX)
else()
    message(STATUS "AAX SDK not used; building ${TUBECHANNEL_FORMATS}")
endif()

juce_add_plugin(TubeChannel
    COMPANY_NAME             "OpenGrid"
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    PLUGIN_MANUFACTURER_CODE OpGr
    PLUGIN_CODE              TbCh
    FORMATS                  ${TUBECHANNEL_FORMATS}
    PRODUCT_NAME             "Tube Channel"
    COPY_PLUGIN_AFTER_BUILD  FALSE
    ${TUBECHANNEL_EXTRA_ARGS}
)

# --- CLAP ----------------------------------------------------------------------------------------------
option(TUBECHANNEL_BUILD_CLAP "Build the CLAP plugin" ON)
if(TUBECHANNEL_BUILD_CLAP)
    set(TUBECHANNEL_CLAP_EXT_SOURCE "" CACHE PATH "Use an existing clap-juce-extensions checkout")
    if(TUBECHANNEL_CLAP_EXT_SOURCE)
        set(FETCHCONTENT_SOURCE_DIR_CLAP-JUCE-EXTENSIONS "${TUBECHANNEL_CLAP_EXT_SOURCE}")
    endif()
    FetchContent_Declare(
        clap-juce-extensions
        GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
        # Pinned commit on main: the 0.26.0 tag predates JUCE 8 and does not compile against it.
        GIT_TAG        55525c9858d4b25687be7759a5e0f70eccef218e
        GIT_SUBMODULES_RECURSE TRUE
    )
    FetchContent_MakeAvailable(clap-juce-extensions)
endif()

# Netlists are embedded into the binary at configure time.
foreach(net mic inst tube preout comp det eq1 eq2 eq3 eq4 out psu)
    string(TOUPPER ${net} NETU)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Netlists/${net}.net")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/Netlists/${net}.net" TC_${NETU})
endforeach()
configure_file(Netlists/EmbeddedNetlists.h.in generated/EmbeddedNetlists.h @ONLY)

target_sources(TubeChannel PRIVATE
    Source/Plugin/PluginProcessor.cpp
    Source/Plugin/PluginEditor.cpp
    Source/Plugin/Params.cpp
)
target_include_directories(TubeChannel PRIVATE Source Source/Plugin "${GOODLOOKINUI_ROOT}/adapters/juce" "${CMAKE_CURRENT_BINARY_DIR}/generated")

target_compile_definitions(TubeChannel PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    TC_EXPERIMENTAL_SUPPLY=$<BOOL:${TUBECHANNEL_EXPERIMENTAL_SUPPLY}>
)

target_link_libraries(TubeChannel
    PRIVATE
        goodlookinui_juce
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Refresh local ad-hoc bundle seals after relinking and resource generation. JUCE writes VST3
# moduleinfo.json after its initial check; Developer ID signing stays in the installer flow.
if(APPLE)
    foreach(local_bundle TubeChannel_VST3 TubeChannel_AU TubeChannel_Standalone)
        add_custom_command(TARGET ${local_bundle} POST_BUILD
            COMMAND ${CMAKE_COMMAND} "-Dsrc=$<TARGET_BUNDLE_DIR:${local_bundle}>"
                -P "${JUCE_SOURCE_DIR}/extras/Build/CMake/checkBundleSigning.cmake"
            VERBATIM)
    endforeach()
endif()

if(TUBECHANNEL_BUILD_CLAP)
    clap_juce_extensions_plugin(TARGET TubeChannel
        CLAP_ID "com.opengrid.tubechannel"
        CLAP_FEATURES audio-effect equalizer compressor mono stereo
        CLAP_MANUAL_URL ""
        CLAP_SUPPORT_URL "")
endif()

# --- Host-level stress test of the real processor --------------------------------------------------
juce_add_console_app(TubeChannelStress PRODUCT_NAME "Tube Channel Stress Test")
target_sources(TubeChannelStress PRIVATE tests/processor_stress.cpp)
target_include_directories(TubeChannelStress PRIVATE Source Source/Plugin "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(TubeChannelStress PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(TubeChannelStress PRIVATE TubeChannel juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
add_test(NAME processor_stress COMMAND TubeChannelStress)
add_test(NAME offline_bounce COMMAND TubeChannelStress offline)

juce_add_console_app(TubeChannelGuiTest PRODUCT_NAME "Tube Channel GUI Test")
target_sources(TubeChannelGuiTest PRIVATE tests/gui_test.cpp)
target_include_directories(TubeChannelGuiTest PRIVATE Source Source/Plugin "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(TubeChannelGuiTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(TubeChannelGuiTest PRIVATE TubeChannel juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
add_test(NAME gui COMMAND TubeChannelGuiTest)

# --- editor snapshot tool (renders both skins to PNG) -------------------------------------------------
juce_add_console_app(TubeChannelSnapshot PRODUCT_NAME "Tube Channel Snapshot")
target_sources(TubeChannelSnapshot PRIVATE tests/ui_snapshot.cpp)
target_include_directories(TubeChannelSnapshot PRIVATE Source Source/Plugin "${GOODLOOKINUI_ROOT}/adapters/juce" "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(TubeChannelSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(TubeChannelSnapshot PRIVATE TubeChannel goodlookinui_juce juce::juce_audio_utils juce::juce_recommended_config_flags)
