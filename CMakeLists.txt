cmake_minimum_required(VERSION 3.25)
project(Halftrack VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 17)
    message(FATAL_ERROR "Halftrack's pinned engine requires AppleClang 17 or newer (Xcode 16.3+).")
endif()
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
endif()
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
include(FetchContent)
# Fetch JUCE separately: the upstream engine submodule uses SSH URLs.
# Full commit and archive hashes make clean builds independent of moving branches.
FetchContent_Declare(juce
    URL https://codeload.github.com/juce-framework/JUCE/tar.gz/37c894f83d379179b2070d437ccd0f1cd9af9576
    URL_HASH SHA256=42524a87f3569273ab7286545816ad1f0cc95240504a7d4464ef1e32c50692aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    # Populate first so the exact-source patch also precedes configure-time juceaide.
    SOURCE_SUBDIR halftrack-populate-only)
FetchContent_MakeAvailable(juce)
include(cmake/PatchChildProcess.cmake)
halftrack_patch_child_process("${juce_SOURCE_DIR}")
add_subdirectory("${juce_SOURCE_DIR}" "${juce_BINARY_DIR}")
set(TE_ADD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(tracktion
    URL https://codeload.github.com/Tracktion/tracktion_engine/tar.gz/07e2d674884293f1a6c8f35fb8d1b9f469ffa41f
    URL_HASH SHA256=d4f0af1514b55f147c13dd68c9ac0effc839a5270541653731e9ad3b23d9fd41
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(tracktion)
include(cmake/PatchTracktion.cmake)
halftrack_patch_tracktion("${tracktion_SOURCE_DIR}")
include(cmake/PatchClipLauncher.cmake)
halftrack_patch_clip_launcher("${tracktion_SOURCE_DIR}")
include(cmake/PatchRenderLatency.cmake)
halftrack_patch_render_latency("${tracktion_SOURCE_DIR}")
include(cmake/PatchPluginScanner.cmake)
halftrack_patch_plugin_scanner("${tracktion_SOURCE_DIR}")
include(cmake/PatchAsyncPlugin.cmake)
halftrack_patch_async_plugin("${tracktion_SOURCE_DIR}")
include(cmake/PatchEditSort.cmake)
halftrack_patch_edit_sort("${tracktion_SOURCE_DIR}")
include(cmake/PatchClipGain.cmake)
halftrack_patch_clip_gain("${tracktion_SOURCE_DIR}")
include(cmake/PatchClipActivation.cmake)
halftrack_patch_clip_activation("${tracktion_SOURCE_DIR}")
include(cmake/PatchReverseChannels.cmake)
halftrack_patch_reverse_channels("${tracktion_SOURCE_DIR}")
include(cmake/PatchWaveInputStart.cmake)
halftrack_patch_wave_input_start("${tracktion_SOURCE_DIR}")
include(cmake/PatchInputRecordingState.cmake)
halftrack_patch_input_recording_state("${tracktion_SOURCE_DIR}")
include(cmake/PatchMidiInputActivation.cmake)
halftrack_patch_midi_input_activation("${tracktion_SOURCE_DIR}")
include(cmake/PatchNegativeTime.cmake)
halftrack_patch_negative_time("${tracktion_SOURCE_DIR}")
include(cmake/PatchMidiDevices.cmake)
halftrack_patch_midi_devices("${tracktion_SOURCE_DIR}")
include(cmake/PatchInputTakeRescan.cmake)
halftrack_patch_input_take_rescan("${tracktion_SOURCE_DIR}")
include(cmake/PatchMidiReleaseVelocity.cmake)
halftrack_patch_midi_release_velocity("${tracktion_SOURCE_DIR}")
include(cmake/PatchTempoRange.cmake)
halftrack_patch_tempo_range("${tracktion_SOURCE_DIR}")
include(cmake/PatchStretchAlignment.cmake)
halftrack_patch_stretch_alignment("${tracktion_SOURCE_DIR}")
include(cmake/PatchLaunchRepeat.cmake)
halftrack_patch_launch_repeat("${tracktion_SOURCE_DIR}")

juce_add_gui_app(Halftrack
    PRODUCT_NAME "Halftrack"
    COMPANY_NAME "Halftrack Contributors"
    BUNDLE_ID "org.halftrack.studio"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Halftrack records audio from the input you choose when you arm a track and press Record."
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE
    PLUGINHOST_AU TRUE)

juce_add_binary_data(HalftrackAssets SOURCES assets/halftrack-mark.svg)
target_sources(Halftrack PRIVATE src/Main.cpp src/Session.cpp src/SubtractiveSynth.cpp src/UtilityProcessor.cpp src/UtilityEditor.cpp src/NativeEffects.cpp src/Racks.cpp src/EffectProcessors.cpp src/MidiEffects.cpp src/Instruments.cpp src/NativeEffectEditor.cpp src/AudioClipEditor.cpp src/AutomationEditor.cpp src/MainComponent.cpp tests/SessionTests.cpp tests/MidiTests.cpp tests/PersistenceTests.cpp tests/LatencyTests.cpp tests/LauncherTests.cpp)
target_include_directories(Halftrack PRIVATE src)
target_compile_definitions(Halftrack PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST=0 JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=$<BOOL:${APPLE}>
    JUCE_ASIO=$<BOOL:${WIN32}>
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=0
    TRACKTION_ENABLE_TIMESTRETCH_SIGNALSMITH=1
    TRACKTION_ENABLE_TIMESTRETCH_RUBBERBAND=0)
target_link_libraries(Halftrack PRIVATE
    tracktion::tracktion_engine tracktion::tracktion_core tracktion::tracktion_graph
    juce::juce_audio_utils juce::juce_recommended_config_flags
    HalftrackAssets)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Tracktion's wider atomic operations use the compiler runtime on Linux.
    target_link_libraries(Halftrack PRIVATE atomic)
endif()
if(APPLE)
    target_compile_options(Halftrack PRIVATE -fno-aligned-allocation)
endif()
include(CTest)
target_compile_definitions(Halftrack PRIVATE HALFTRACK_TESTS_ENABLED=$<BOOL:${BUILD_TESTING}>)
if(BUILD_TESTING)
    target_sources(Halftrack PRIVATE tests/RealtimeTests.cpp tests/PluginScanTests.cpp tests/PluginEditorTests.cpp tests/SendReturnTests.cpp tests/UtilityTests.cpp tests/NativeEffectsTests.cpp tests/ExtendedEffectsTests.cpp tests/MidiEffectsTests.cpp tests/InstrumentsTests.cpp tests/WarpTests.cpp tests/GroupTests.cpp tests/LaunchModeTests.cpp tests/SamplerTests.cpp tests/RackTests.cpp tests/DeviceAutomationTests.cpp tests/FreezeTests.cpp tests/StemExportTests.cpp tests/CrossfadeTests.cpp tests/TimeEditTests.cpp tests/NoteToolsTests.cpp tests/AudioClipTests.cpp tests/LocatorTests.cpp tests/MidiFileTests.cpp tests/TrackOrderTests.cpp tests/TrackDuplicateTests.cpp tests/TempoTests.cpp tests/MidiInputTests.cpp tests/MetronomeTests.cpp tests/MasterVolumeTests.cpp tests/ClipActivationTests.cpp tests/AudioExportTests.cpp tests/FollowActionTests.cpp tests/AutomationTests.cpp)
    set(HALFTRACK_GAIN_TEST_FORMATS VST3)
    if(APPLE)
        list(APPEND HALFTRACK_GAIN_TEST_FORMATS AU)
        target_sources(Halftrack PRIVATE tests/AudioUnitTests.cpp tests/AudioUnitTestRegistration.cpp)
    endif()
    juce_add_plugin(HalftrackTestGain
        BUNDLE_ID "org.halftrack.testgain"
        COMPANY_NAME "Halftrack Contributors"
        PLUGIN_MANUFACTURER_CODE Htrk
        PLUGIN_CODE Htgn
        FORMATS ${HALFTRACK_GAIN_TEST_FORMATS}
        PRODUCT_NAME "Halftrack Test Gain"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE)
    target_sources(HalftrackTestGain PRIVATE tests/TestGainPlugin.cpp)
    target_compile_definitions(HalftrackTestGain PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(HalftrackTestGain PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
    juce_add_plugin(HalftrackTestLatency
        BUNDLE_ID "org.halftrack.testlatency"
        COMPANY_NAME "Halftrack Contributors"
        PLUGIN_MANUFACTURER_CODE Htrk
        PLUGIN_CODE Htlt
        FORMATS VST3
        PRODUCT_NAME "Halftrack Test Latency"
        IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE)
    target_sources(HalftrackTestLatency PRIVATE tests/TestLatencyPlugin.cpp)
    target_compile_definitions(HalftrackTestLatency PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(HalftrackTestLatency PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
    add_dependencies(Halftrack HalftrackTestGain_VST3 HalftrackTestLatency_VST3)
    if(APPLE)
        add_dependencies(Halftrack HalftrackTestGain_AU)
        add_test(NAME au-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-au ${CMAKE_BINARY_DIR}/test-artifacts)
        set_tests_properties(au-acceptance PROPERTIES TIMEOUT 90
            ENVIRONMENT "HALFTRACK_TEST_AU_PLUGIN=$<TARGET_FILE_DIR:HalftrackTestGain_AU>/../..")
    endif()
    add_test(NAME clip-activation-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-clip-activation ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(clip-activation-acceptance PROPERTIES TIMEOUT 180)
    add_test(NAME master-volume-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-master-volume ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(master-volume-acceptance PROPERTIES TIMEOUT 90)
    add_test(NAME follow-actions-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-follow-actions ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(follow-actions-acceptance PROPERTIES TIMEOUT 120)
    add_test(NAME automation-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-automation ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(automation-acceptance PROPERTIES TIMEOUT 180)
    add_test(NAME automation-editor-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-automation-editor ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(automation-editor-acceptance PROPERTIES TIMEOUT 90
        ENVIRONMENT "HALFTRACK_TEST_UI_SNAPSHOT_DIRECTORY=${CMAKE_BINARY_DIR}/test-artifacts/ui-snapshots")
    add_test(NAME audio-export-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-audio-export ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(audio-export-acceptance PROPERTIES TIMEOUT 180)
    add_test(NAME metronome-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-metronome ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(metronome-acceptance PROPERTIES TIMEOUT 300)
    add_test(NAME midi-input-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-midi-input ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(midi-input-acceptance PROPERTIES TIMEOUT 300)
    add_test(NAME midi-monitor-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-midi-monitor)
    set_tests_properties(midi-monitor-acceptance PROPERTIES TIMEOUT 90 ENVIRONMENT "HALFTRACK_TEST_DISPATCH_MS=7")
    add_test(NAME tempo-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-tempo ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(tempo-acceptance PROPERTIES TIMEOUT 90)
    add_test(NAME track-duplicate-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-track-duplicate ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(track-duplicate-acceptance PROPERTIES TIMEOUT 90
        ENVIRONMENT "HALFTRACK_TEST_PLUGIN=$<TARGET_FILE_DIR:HalftrackTestGain_VST3>/../..")
    add_test(NAME track-order-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-track-order ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(track-order-acceptance PROPERTIES TIMEOUT 90)
    add_test(NAME midi-files-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-midi-files ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(midi-files-acceptance PROPERTIES TIMEOUT 90)
    add_test(NAME locators-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-locators ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(locators-acceptance PROPERTIES TIMEOUT 90)
    add_test(NAME audio-clips-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-audio-clips ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(audio-clips-acceptance PROPERTIES TIMEOUT 180)
    add_test(NAME note-tools-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-note-tools ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(note-tools-acceptance PROPERTIES TIMEOUT 90)
    add_test(NAME groups-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-groups ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(groups-acceptance PROPERTIES TIMEOUT 180)
    add_test(NAME time-edit-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-time-edit ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(time-edit-acceptance PROPERTIES TIMEOUT 180)
    add_test(NAME crossfade-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-crossfade ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(crossfade-acceptance PROPERTIES TIMEOUT 180)
    add_test(NAME stems-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-stems ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(stems-acceptance PROPERTIES TIMEOUT 180)
    add_test(NAME freeze-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-freeze ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(freeze-acceptance PROPERTIES TIMEOUT 180)
    add_test(NAME device-automation-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-device-automation ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(device-automation-acceptance PROPERTIES TIMEOUT 180)
    add_test(NAME racks-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-racks ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(racks-acceptance PROPERTIES TIMEOUT 240)
    add_test(NAME sampler-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-sampler ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(sampler-acceptance PROPERTIES TIMEOUT 180)
    add_test(NAME launch-modes-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-launch-modes ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(launch-modes-acceptance PROPERTIES TIMEOUT 180)
    add_test(NAME warp-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-warp ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(warp-acceptance PROPERTIES TIMEOUT 240)
    add_test(NAME instruments-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-instruments ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(instruments-acceptance PROPERTIES TIMEOUT 240)
    add_test(NAME midi-effects-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-midi-effects ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(midi-effects-acceptance PROPERTIES TIMEOUT 180)
    add_test(NAME native-effects-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-effects ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(native-effects-acceptance PROPERTIES TIMEOUT 180)
    add_test(NAME utility-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-utility ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(utility-acceptance PROPERTIES TIMEOUT 180)
    add_test(NAME sends-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-sends ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(sends-acceptance PROPERTIES TIMEOUT 180
        ENVIRONMENT "HALFTRACK_TEST_PLUGIN=$<TARGET_FILE_DIR:HalftrackTestGain_VST3>/../..;HALFTRACK_TEST_LATENCY_PLUGIN=$<TARGET_FILE_DIR:HalftrackTestLatency_VST3>/../..")
    add_test(NAME plugin-editor-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-plugin-editor ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(plugin-editor-acceptance PROPERTIES TIMEOUT 90)
    add_test(NAME plugin-scan-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-plugin-scan ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(plugin-scan-acceptance PROPERTIES TIMEOUT 90
        ENVIRONMENT "HALFTRACK_TEST_PLUGIN=$<TARGET_FILE_DIR:HalftrackTestGain_VST3>/../..")
    add_test(NAME latency-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-latency ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(latency-acceptance PROPERTIES TIMEOUT 90
        ENVIRONMENT "HALFTRACK_TEST_LATENCY_PLUGIN=$<TARGET_FILE_DIR:HalftrackTestLatency_VST3>/../..")
    add_test(NAME export-ui-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-export-ui)
    set_tests_properties(export-ui-acceptance PROPERTIES TIMEOUT 90
        ENVIRONMENT "HALFTRACK_TEST_UI_SNAPSHOT_DIRECTORY=${CMAKE_BINARY_DIR}/test-artifacts/ui-snapshots")
    add_test(NAME ui-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-ui)
    set_tests_properties(ui-acceptance PROPERTIES TIMEOUT 90
        ENVIRONMENT "HALFTRACK_TEST_UI_SNAPSHOT_DIRECTORY=${CMAKE_BINARY_DIR}/test-artifacts/ui-snapshots")
    add_test(NAME launcher-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test-launcher ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(launcher-acceptance PROPERTIES TIMEOUT 120)
    add_test(NAME session-acceptance COMMAND $<TARGET_FILE:Halftrack> --self-test ${CMAKE_BINARY_DIR}/test-artifacts)
    set_tests_properties(session-acceptance PROPERTIES TIMEOUT 180
        ENVIRONMENT "HALFTRACK_TEST_PLUGIN=$<TARGET_FILE_DIR:HalftrackTestGain_VST3>/../..")
endif()
