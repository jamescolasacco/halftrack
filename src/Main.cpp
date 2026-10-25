#include "BuildInfo.h"
#include "MainComponent.h"
#include "Session.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>
#include <algorithm>
#include <iostream>

namespace halftrack
{
int runPersistenceTests (const juce::File&);
int runRecordingTests (const juce::File&);
int runLatencyTests (const juce::File&);
int runLauncherTests (const juce::File&);
int runSettingsIsolationProbe (const juce::File&);
#if HALFTRACK_TESTS_ENABLED
int runUtilityTests (const juce::File&);
int runNativeEffectsTests (const juce::File&);
int runNoteToolsTests (const juce::File&);
int runAudioClipTests (const juce::File&);
int runLocatorTests (const juce::File&);
int runMidiFileTests (const juce::File&);
int runTrackOrderTests (const juce::File&);
int runTrackDuplicateTests (const juce::File&);
int runTempoTests (const juce::File&);
int runMidiInputTests (const juce::File&);
int runMidiMonitoringTests();
int runMetronomeTests (const juce::File&);
int runMasterVolumeTests (const juce::File&);
int runAudioExportTests (const juce::File&);
int runFollowActionTests (const juce::File&);
int runAutomationTests (const juce::File&);
int runAutomationEditorTests (const juce::File&);
int runClipActivationTests (const juce::File&);
int runRealtimeTests();
int runMidiEffectsTests (const juce::File&);
int runInstrumentsTests (const juce::File&);
int runWarpTests (const juce::File&);
int runLaunchModeTests (const juce::File&);
int runSamplerTests (const juce::File&);
int runDeviceAutomationTests (const juce::File&);
int runFreezeTests (const juce::File&);
int runStemExportTests (const juce::File&);
int runCrossfadeTests (const juce::File&);
int runTimeEditTests (const juce::File&);
int runTrackDelayTests (const juce::File&);
int runTrackColourTests (const juce::File&);
int runConvertMidiTests (const juce::File&);
int runMidiControllerTests (const juce::File&);
int runRangeEditTests (const juce::File&);
int runTimeSignatureTests (const juce::File&);
int runMidiToolTests (const juce::File&);
int runRecoveryTests (const juce::File&);
int runPresetTests (const juce::File&);
int runTempoMapTests (const juce::File&);
int runGrooveTests (const juce::File&);
int runTunerTests (const juce::File&);
int runAutomationCaptureTests (const juce::File&);
int runPunchTests (const juce::File&);
int runSpectrumTests (const juce::File&);
int runPreviewTests (const juce::File&);
int runMidiMonitorTests (const juce::File&);
int runTransportTests (const juce::File&);
int runMissingMediaTests (const juce::File&);
int runPluginAutomationTests (const juce::File&);
int runCaptureMidiTests (const juce::File&);
int runTakeLanesTests (const juce::File&);
int runRoutingTests (const juce::File&);
int runSceneTempoTests (const juce::File&);
int runAudioFromTests (const juce::File&);
int runAudioEngineTests (const juce::File&);
int runMultitrackRecordingTests (const juce::File&);
int runSessionRecordTests (const juce::File&);
int runNoteChanceTests (const juce::File&);
int runRepitchTests (const juce::File&);
int runSharedMediaTests (const juce::File&);
int runWavDecoderTests (const juce::File&);
int runRenderAgreementTests (const juce::File&);
int runRecordingFormatTests (const juce::File&);
int runRecordingStallTests (const juce::File&);
int runDeviceSettingsTests (const juce::File&);
int runCollectionsTests (const juce::File&);
int runPluginPresetTests (const juce::File&);
int runClipEnvelopeTests (const juce::File&);
int runFaderLawTests (const juce::File&);
int runWarpModeTests (const juce::File&);
int runSliceTests (const juce::File&);
int runMpeTests (const juce::File&);
int runInputConfigTests (const juce::File&);
int runFuzzTests (const juce::File&);
int runGraphEditTests (const juce::File&);
int runStuckNoteTests (const juce::File&);
int runLargeSessionTests (const juce::File&);
int runPreferencesTests (const juce::File&);
int runLinkedTrackTests (const juce::File&);
int runBrowserIndexTests (const juce::File&);
int runParameterIdentityTests (const juce::File&);
int runComputerKeyboardTests (const juce::File&);
int runKeyMapTests (const juce::File&);
int runAutoArmTests (const juce::File&);
int runDeviceLossTests (const juce::File&);
int runSoakTests (const juce::File&);
int runPluginTimingTests (const juce::File&);
int runPluginBinaryTests (const juce::File&);
int runSidechainTests (const juce::File&);
int runMidiPortTests (const juce::File&);
int runThemeTests (const juce::File&);
int runInputPermissionTests (const juce::File&);
int runSimilarityTests (const juce::File&);
int runTempoAnalysisTests (const juce::File&);
int runClipLoopTests (const juce::File&);
int runRangeClipboardTests (const juce::File&);
int runRecordingLatencyTests (const juce::File&);
int runVideoTests (const juce::File&);
int runExternalSyncTests (const juce::File&);
int runSceneCaptureTests (const juce::File&);
int runSlotClipTests (const juce::File&);
int runEditGridTests (const juce::File&);
int runNotePreviewTests (const juce::File&);
int runPluginSourcesTests (const juce::File&);
int runClipFadeTests (const juce::File&);
int runOutputRoutingTests (const juce::File&);
int runTempoAnalysisEvaluation (const juce::File& library, const juce::File& report);
int runPackTests (const juce::File&);
int runMackieTests (const juce::File&);
int runSimilarityEvaluation (const juce::File& library, int perCategory, const juce::File& report);
int runHardwareCheck (const juce::File&, const juce::String& deviceName, const juce::String& mode);
int runMidiMappingTests (const juce::File&);
int runGroupTests (const juce::File&);
int runPluginScanTests (const juce::File&);
int runPluginEditorTests (const juce::File&);
int runSendReturnTests (const juce::File&);
int runRackTests (const juce::File&);
#if JUCE_MAC
juce::Result registerAudioUnitTestFixture();
int runAudioUnitTests (const juce::File&);
#endif
#endif
class Application final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Halftrack"; }
    const juce::String getApplicationVersion() override { return halftrack::buildinfo::version(); }
    // The isolated plug-in scanner must be allowed to start a child instance.
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String& commandLine) override
    {
        #if HALFTRACK_TESTS_ENABLED && JUCE_MAC
        if (auto registered = registerAudioUnitTestFixture(); registered.failed())
        {
            std::cerr << registered.getErrorMessage() << std::endl;
            setApplicationReturnValue (1);
            quit();
            return;
        }
        #endif
        if (tracktion::PluginManager::startChildProcessPluginScan (commandLine))
            return;
        auto args = juce::StringArray::fromTokens (commandLine, true);
        args.removeEmptyStrings();
        for (auto& arg : args) arg = arg.unquoted();
        // Prints what this build is and quits, without opening audio devices or a window.
        if (args.contains ("--version"))
        {
            std::cout << halftrack::buildinfo::summary() << std::endl;
            quit();
            return;
        }
        #if HALFTRACK_TESTS_ENABLED
        // A suite runs without a window or real audio, so macOS would nap the process and stretch
        // every timer and dispatch; it runs at full speed instead, as when the interface plays.
        std::unique_ptr<juce::ScopedLowPowerModeDisabler> fullSpeed;
        for (const auto& arg : args) if (arg.startsWith ("--self-test")) { fullSpeed = std::make_unique<juce::ScopedLowPowerModeDisabler>(); break; }
        // A suite that crashes prints where, since CI keeps no core files. Printing in a signal
        // handler is not safe in general, but the process is ending anyway.
        for (const auto& arg : args)
            if (arg.startsWith ("--self-test"))
            {
                juce::SystemStats::setApplicationCrashHandler ([] (void*)
                { std::cerr << "Halftrack self-test crashed. Stack:\n" << juce::SystemStats::getStackBacktrace() << std::endl; });
                break;
            }
        #if JUCE_MAC
        if (args.contains ("--self-test-au"))
        {
            const int i = args.indexOf ("--self-test-au");
            const auto destination = i + 1 < args.size()
                ? juce::File (args[i + 1])
                : juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("halftrack-tests");
            setApplicationReturnValue (runAudioUnitTests (destination));
            quit();
            return;
        }
        #endif
        if (args.contains ("--self-test-clip-activation"))
        {
            const int i=args.indexOf("--self-test-clip-activation");
            const auto destination=i+1<args.size()?juce::File(args[i+1])
                :juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("halftrack-tests");
            setApplicationReturnValue(runClipActivationTests(destination));quit();return;
        }
        if (args.contains ("--self-test-master-volume"))
        {
            const int i=args.indexOf("--self-test-master-volume");
            const auto destination=i+1<args.size()?juce::File(args[i+1])
                :juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("halftrack-tests");
            setApplicationReturnValue(runMasterVolumeTests(destination));quit();return;
        }
        if (args.contains ("--self-test-follow-actions"))
        {
            const int i = args.indexOf ("--self-test-follow-actions");
            const auto destination = i + 1 < args.size() ? juce::File (args[i + 1])
                : juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("halftrack-tests");
            setApplicationReturnValue (runFollowActionTests (destination)); quit(); return;
        }
        if (args.contains ("--self-test-automation"))
        {
            const int i = args.indexOf ("--self-test-automation");
            const auto destination = i + 1 < args.size() ? juce::File (args[i + 1])
                : juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("halftrack-tests");
            setApplicationReturnValue (runAutomationTests (destination)); quit(); return;
        }
        if (args.contains ("--self-test-automation-editor"))
        {
            const int i = args.indexOf ("--self-test-automation-editor");
            const auto destination = i + 1 < args.size() ? juce::File (args[i + 1])
                : juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("halftrack-tests");
            setApplicationReturnValue (runAutomationEditorTests (destination)); quit(); return;
        }
        if (args.contains ("--self-test-audio-export"))
        {
            const int i = args.indexOf ("--self-test-audio-export");
            const auto destination = i + 1 < args.size() ? juce::File (args[i + 1])
                : juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("halftrack-tests");
            setApplicationReturnValue (runAudioExportTests (destination)); quit(); return;
        }
        if (args.contains ("--self-test-metronome"))
        {
            const int i=args.indexOf("--self-test-metronome");
            const auto destination=i+1<args.size()?juce::File(args[i+1])
                :juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("halftrack-tests");
            setApplicationReturnValue(runMetronomeTests(destination));quit();return;
        }
        if (args.contains ("--self-test-midi-monitor"))
        {
            setApplicationReturnValue(runMidiMonitoringTests());quit();return;
        }
        if (args.contains ("--self-test-midi-input"))
        {
            const int i = args.indexOf ("--self-test-midi-input");
            const auto destination = i + 1 < args.size() ? juce::File (args[i + 1])
                : juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("halftrack-tests");
            setApplicationReturnValue (runMidiInputTests (destination)); quit(); return;
        }
        if (args.contains ("--self-test-tempo"))
        {
            const int i = args.indexOf ("--self-test-tempo");
            const auto destination = i + 1 < args.size() ? juce::File (args[i + 1])
                : juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("halftrack-tests");
            setApplicationReturnValue (runTempoTests (destination)); quit(); return;
        }
        if (args.contains ("--self-test-track-duplicate"))
        {
            const int i = args.indexOf ("--self-test-track-duplicate");
            const auto destination = i + 1 < args.size() ? juce::File (args[i + 1])
                : juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("halftrack-tests");
            setApplicationReturnValue (runTrackDuplicateTests (destination)); quit(); return;
        }
        if (args.contains ("--self-test-track-order"))
        {
            const int i = args.indexOf ("--self-test-track-order");
            const auto destination = i + 1 < args.size() ? juce::File (args[i + 1])
                : juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("halftrack-tests");
            setApplicationReturnValue (runTrackOrderTests (destination)); quit(); return;
        }
        if (args.contains ("--self-test-midi-files"))
        {
            const int i = args.indexOf ("--self-test-midi-files");
            const auto destination = i + 1 < args.size() ? juce::File (args[i + 1])
                : juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("halftrack-tests");
            setApplicationReturnValue (runMidiFileTests (destination)); quit(); return;
        }
        if (args.contains ("--self-test-locators"))
        {
            const int i = args.indexOf ("--self-test-locators");
            const auto destination = i + 1 < args.size() ? juce::File (args[i + 1])
                : juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("halftrack-tests");
            setApplicationReturnValue (runLocatorTests (destination)); quit(); return;
        }
        if (args.contains ("--self-test-audio-clips"))
        {
            const int i = args.indexOf ("--self-test-audio-clips");
            const auto destination = i + 1 < args.size() ? juce::File (args[i + 1])
                : juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("halftrack-tests");
            setApplicationReturnValue (runAudioClipTests (destination)); quit(); return;
        }
        if (args.contains ("--self-test-note-tools"))
        {
            const int i = args.indexOf ("--self-test-note-tools");
            const auto destination = i + 1 < args.size() ? juce::File (args[i + 1])
                : juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("halftrack-tests");
            setApplicationReturnValue (runNoteToolsTests (destination)); quit(); return;
        }
        if (args.contains ("--self-test-groups"))
        {
            const int i = args.indexOf ("--self-test-groups");
            setApplicationReturnValue (runGroupTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-midi-mapping"))
        {
            const int i = args.indexOf ("--self-test-midi-mapping");
            setApplicationReturnValue (runMidiMappingTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-routing"))
        {
            const int i = args.indexOf ("--self-test-routing");
            setApplicationReturnValue (runRoutingTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-preferences"))
        {
            const int i = args.indexOf ("--self-test-preferences");
            setApplicationReturnValue (runPreferencesTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-linked-tracks"))
        {
            const int i = args.indexOf ("--self-test-linked-tracks");
            setApplicationReturnValue (runLinkedTrackTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-browser-index"))
        {
            const int i = args.indexOf ("--self-test-browser-index");
            setApplicationReturnValue (runBrowserIndexTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-parameter-identity"))
        {
            const int i = args.indexOf ("--self-test-parameter-identity");
            setApplicationReturnValue (runParameterIdentityTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-computer-keyboard"))
        {
            const int i = args.indexOf ("--self-test-computer-keyboard");
            setApplicationReturnValue (runComputerKeyboardTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-key-map"))
        {
            const int i = args.indexOf ("--self-test-key-map");
            setApplicationReturnValue (runKeyMapTests (juce::File (args[i + 1])));
            quit(); return;
        }
        // A manual check on a real audio interface, never run by CTest (see tests/HardwareCheck.cpp).
        if (args.contains ("--hardware-check"))
        {
            const int i = args.indexOf ("--hardware-check");
            const auto device = args[i + 2].isNotEmpty() && ! args[i + 2].startsWith ("-") && args[i + 2] != "loopback" && args[i + 2] != "unplug" ? args[i + 2] : juce::String ("M4");
            const auto mode = args.contains ("loopback") ? juce::String ("loopback") : args.contains ("unplug") ? juce::String ("unplug") : juce::String();
            setApplicationReturnValue (runHardwareCheck (juce::File (args[i + 1]), device, mode));
            quit(); return;
        }
        if (args.contains ("--self-test-auto-arm"))
        {
            const int i = args.indexOf ("--self-test-auto-arm");
            setApplicationReturnValue (runAutoArmTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-device-loss"))
        {
            const int i = args.indexOf ("--self-test-device-loss");
            setApplicationReturnValue (runDeviceLossTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-soak"))
        {
            const int i = args.indexOf ("--self-test-soak");
            setApplicationReturnValue (runSoakTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-plugin-timing"))
        {
            const int i = args.indexOf ("--self-test-plugin-timing");
            setApplicationReturnValue (runPluginTimingTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-plugin-binary"))
        {
            const int i = args.indexOf ("--self-test-plugin-binary");
            setApplicationReturnValue (runPluginBinaryTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-sidechain"))
        {
            const int i = args.indexOf ("--self-test-sidechain");
            setApplicationReturnValue (runSidechainTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-midi-ports"))
        {
            const int i = args.indexOf ("--self-test-midi-ports");
            setApplicationReturnValue (runMidiPortTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-themes"))
        {
            const int i = args.indexOf ("--self-test-themes");
            setApplicationReturnValue (runThemeTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-input-permission"))
        {
            const int i = args.indexOf ("--self-test-input-permission");
            setApplicationReturnValue (runInputPermissionTests (juce::File (args[i + 1])));
            quit(); return;
        }
        // Manual: --tempo-evaluate <library folder> [report.json]
        if (args.contains ("--tempo-evaluate"))
        {
            const int i = args.indexOf ("--tempo-evaluate");
            setApplicationReturnValue (runTempoAnalysisEvaluation (juce::File (args[i + 1]), i + 2 < args.size() ? juce::File (args[i + 2]) : juce::File()));
            quit();
            return;
        }
        // Manual: --similarity-evaluate <library folder> [sounds per category] [report.json]
        if (args.contains ("--similarity-evaluate"))
        {
            const int i = args.indexOf ("--similarity-evaluate");
            setApplicationReturnValue (runSimilarityEvaluation (juce::File (args[i + 1]), i + 2 < args.size() ? args[i + 2].getIntValue() : 60,
                                                                i + 3 < args.size() ? juce::File (args[i + 3]) : juce::File()));
            quit();
            return;
        }
        if (args.contains ("--self-test-similarity"))
        {
            const int i = args.indexOf ("--self-test-similarity");
            setApplicationReturnValue (runSimilarityTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-scene-capture"))
        {
            const int i = args.indexOf ("--self-test-scene-capture");
            setApplicationReturnValue (runSceneCaptureTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-slot-clips"))
        {
            const int i = args.indexOf ("--self-test-slot-clips");
            setApplicationReturnValue (runSlotClipTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-edit-grid"))
        {
            const int i = args.indexOf ("--self-test-edit-grid");
            setApplicationReturnValue (runEditGridTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-output-routing"))
        {
            const int i = args.indexOf ("--self-test-output-routing");
            setApplicationReturnValue (runOutputRoutingTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-clip-fades"))
        {
            const int i = args.indexOf ("--self-test-clip-fades");
            setApplicationReturnValue (runClipFadeTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-plugin-sources"))
        {
            const int i = args.indexOf ("--self-test-plugin-sources");
            setApplicationReturnValue (runPluginSourcesTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-note-preview"))
        {
            const int i = args.indexOf ("--self-test-note-preview");
            setApplicationReturnValue (runNotePreviewTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-external-sync"))
        {
            const int i = args.indexOf ("--self-test-external-sync");
            setApplicationReturnValue (runExternalSyncTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-video"))
        {
            const int i = args.indexOf ("--self-test-video");
            setApplicationReturnValue (runVideoTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-recording-latency"))
        {
            const int i = args.indexOf ("--self-test-recording-latency");
            setApplicationReturnValue (runRecordingLatencyTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-range-clipboard"))
        {
            const int i = args.indexOf ("--self-test-range-clipboard");
            setApplicationReturnValue (runRangeClipboardTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-clip-loops"))
        {
            const int i = args.indexOf ("--self-test-clip-loops");
            setApplicationReturnValue (runClipLoopTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-tempo-analysis"))
        {
            const int i = args.indexOf ("--self-test-tempo-analysis");
            setApplicationReturnValue (runTempoAnalysisTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-packs"))
        {
            const int i = args.indexOf ("--self-test-packs");
            setApplicationReturnValue (runPackTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-mackie"))
        {
            const int i = args.indexOf ("--self-test-mackie");
            setApplicationReturnValue (runMackieTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-large-session"))
        {
            const int i = args.indexOf ("--self-test-large-session");
            setApplicationReturnValue (runLargeSessionTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-stuck-notes"))
        {
            const int i = args.indexOf ("--self-test-stuck-notes");
            setApplicationReturnValue (runStuckNoteTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-graph-edits"))
        {
            const int i = args.indexOf ("--self-test-graph-edits");
            setApplicationReturnValue (runGraphEditTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-fuzz"))
        {
            const int i = args.indexOf ("--self-test-fuzz");
            setApplicationReturnValue (runFuzzTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-input-config"))
        {
            const int i = args.indexOf ("--self-test-input-config");
            setApplicationReturnValue (runInputConfigTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-mpe"))
        {
            const int i = args.indexOf ("--self-test-mpe");
            setApplicationReturnValue (runMpeTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-slicing"))
        {
            const int i = args.indexOf ("--self-test-slicing");
            setApplicationReturnValue (runSliceTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-warp-modes"))
        {
            const int i = args.indexOf ("--self-test-warp-modes");
            setApplicationReturnValue (runWarpModeTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-clip-envelopes"))
        {
            const int i = args.indexOf ("--self-test-clip-envelopes");
            setApplicationReturnValue (runClipEnvelopeTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-fader-laws"))
        {
            const int i = args.indexOf ("--self-test-fader-laws");
            setApplicationReturnValue (runFaderLawTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-plugin-presets"))
        {
            const int i = args.indexOf ("--self-test-plugin-presets");
            setApplicationReturnValue (runPluginPresetTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-collections"))
        {
            const int i = args.indexOf ("--self-test-collections");
            setApplicationReturnValue (runCollectionsTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-device-settings"))
        {
            const int i = args.indexOf ("--self-test-device-settings");
            setApplicationReturnValue (runDeviceSettingsTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-recording-format"))
        {
            const int i = args.indexOf ("--self-test-recording-format");
            setApplicationReturnValue (runRecordingFormatTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-recording-stalls"))
        {
            const int i = args.indexOf ("--self-test-recording-stalls");
            setApplicationReturnValue (runRecordingStallTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-shared-media"))
        {
            const int i = args.indexOf ("--self-test-shared-media");
            setApplicationReturnValue (runSharedMediaTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-wav-decoder"))
        {
            const int i = args.indexOf ("--self-test-wav-decoder");
            setApplicationReturnValue (runWavDecoderTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-render-agreement"))
        {
            const int i = args.indexOf ("--self-test-render-agreement");
            setApplicationReturnValue (runRenderAgreementTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-repitch"))
        {
            const int i = args.indexOf ("--self-test-repitch");
            setApplicationReturnValue (runRepitchTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-note-chance"))
        {
            const int i = args.indexOf ("--self-test-note-chance");
            setApplicationReturnValue (runNoteChanceTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-session-record"))
        {
            const int i = args.indexOf ("--self-test-session-record");
            setApplicationReturnValue (runSessionRecordTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-multitrack-recording"))
        {
            const int i = args.indexOf ("--self-test-multitrack-recording");
            setApplicationReturnValue (runMultitrackRecordingTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-audio-engine"))
        {
            const int i = args.indexOf ("--self-test-audio-engine");
            setApplicationReturnValue (runAudioEngineTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-audio-from"))
        {
            const int i = args.indexOf ("--self-test-audio-from");
            setApplicationReturnValue (runAudioFromTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-scene-tempo"))
        {
            const int i = args.indexOf ("--self-test-scene-tempo");
            setApplicationReturnValue (runSceneTempoTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-take-lanes"))
        {
            const int i = args.indexOf ("--self-test-take-lanes");
            setApplicationReturnValue (runTakeLanesTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-capture-midi"))
        {
            const int i = args.indexOf ("--self-test-capture-midi");
            setApplicationReturnValue (runCaptureMidiTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-plugin-automation"))
        {
            const int i = args.indexOf ("--self-test-plugin-automation");
            setApplicationReturnValue (runPluginAutomationTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-missing-media"))
        {
            const int i = args.indexOf ("--self-test-missing-media");
            setApplicationReturnValue (runMissingMediaTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-transport"))
        {
            const int i = args.indexOf ("--self-test-transport");
            setApplicationReturnValue (runTransportTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-midi-monitor-device"))
        {
            const int i = args.indexOf ("--self-test-midi-monitor-device");
            setApplicationReturnValue (runMidiMonitorTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-preview"))
        {
            const int i = args.indexOf ("--self-test-preview");
            setApplicationReturnValue (runPreviewTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-spectrum"))
        {
            const int i = args.indexOf ("--self-test-spectrum");
            setApplicationReturnValue (runSpectrumTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-punch"))
        {
            const int i = args.indexOf ("--self-test-punch");
            setApplicationReturnValue (runPunchTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-automation-capture"))
        {
            const int i = args.indexOf ("--self-test-automation-capture");
            setApplicationReturnValue (runAutomationCaptureTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-tuner"))
        {
            const int i = args.indexOf ("--self-test-tuner");
            setApplicationReturnValue (runTunerTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-grooves"))
        {
            const int i = args.indexOf ("--self-test-grooves");
            setApplicationReturnValue (runGrooveTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-tempo-map"))
        {
            const int i = args.indexOf ("--self-test-tempo-map");
            setApplicationReturnValue (runTempoMapTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-presets"))
        {
            const int i = args.indexOf ("--self-test-presets");
            setApplicationReturnValue (runPresetTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-recovery"))
        {
            const int i = args.indexOf ("--self-test-recovery");
            setApplicationReturnValue (runRecoveryTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-midi-tools"))
        {
            const int i = args.indexOf ("--self-test-midi-tools");
            setApplicationReturnValue (runMidiToolTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-time-signature"))
        {
            const int i = args.indexOf ("--self-test-time-signature");
            setApplicationReturnValue (runTimeSignatureTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-range-edit"))
        {
            const int i = args.indexOf ("--self-test-range-edit");
            setApplicationReturnValue (runRangeEditTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-midi-controllers"))
        {
            const int i = args.indexOf ("--self-test-midi-controllers");
            setApplicationReturnValue (runMidiControllerTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-track-delay"))
        {
            const int i = args.indexOf ("--self-test-track-delay");
            setApplicationReturnValue (runTrackDelayTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-convert-midi"))
        {
            const int i = args.indexOf ("--self-test-convert-midi");
            setApplicationReturnValue (runConvertMidiTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-track-colours"))
        {
            const int i = args.indexOf ("--self-test-track-colours");
            setApplicationReturnValue (runTrackColourTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-time-edit"))
        {
            const int i = args.indexOf ("--self-test-time-edit");
            setApplicationReturnValue (runTimeEditTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-crossfade"))
        {
            const int i = args.indexOf ("--self-test-crossfade");
            setApplicationReturnValue (runCrossfadeTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-stems"))
        {
            const int i = args.indexOf ("--self-test-stems");
            setApplicationReturnValue (runStemExportTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-freeze"))
        {
            const int i = args.indexOf ("--self-test-freeze");
            setApplicationReturnValue (runFreezeTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-device-automation"))
        {
            const int i = args.indexOf ("--self-test-device-automation");
            setApplicationReturnValue (runDeviceAutomationTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-racks"))
        {
            const int i = args.indexOf ("--self-test-racks");
            setApplicationReturnValue (runRackTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-sampler"))
        {
            const int i = args.indexOf ("--self-test-sampler");
            setApplicationReturnValue (runSamplerTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-launch-modes"))
        {
            const int i = args.indexOf ("--self-test-launch-modes");
            setApplicationReturnValue (runLaunchModeTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-warp"))
        {
            const int i = args.indexOf ("--self-test-warp");
            setApplicationReturnValue (runWarpTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-instruments"))
        {
            const int i = args.indexOf ("--self-test-instruments");
            setApplicationReturnValue (runInstrumentsTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-midi-effects"))
        {
            const int i = args.indexOf ("--self-test-midi-effects");
            setApplicationReturnValue (runMidiEffectsTests (juce::File (args[i + 1])));
            quit(); return;
        }
        if (args.contains ("--self-test-effects"))
        {
            const int i = args.indexOf ("--self-test-effects");
            const auto destination = i + 1 < args.size() ? juce::File (args[i + 1])
                : juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("halftrack-tests");
            setApplicationReturnValue (runNativeEffectsTests (destination)); quit(); return;
        }
        if (args.contains ("--self-test-utility"))
        {
            const int i = args.indexOf ("--self-test-utility");
            const auto destination = i + 1 < args.size() ? juce::File (args[i + 1])
                : juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("halftrack-tests");
            setApplicationReturnValue (runUtilityTests (destination)); quit(); return;
        }
        if (args.contains ("--self-test-sends"))
        {
            const int i = args.indexOf ("--self-test-sends");
            const auto destination = i + 1 < args.size()
                ? juce::File (args[i + 1])
                : juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("halftrack-tests");
            setApplicationReturnValue (runSendReturnTests (destination));
            quit();
            return;
        }
        if (args.contains ("--self-test-plugin-editor"))
        {
            const int i = args.indexOf ("--self-test-plugin-editor");
            const auto destination = i + 1 < args.size()
                ? juce::File (args[i + 1])
                : juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("halftrack-tests");
            setApplicationReturnValue (runPluginEditorTests (destination));
            quit();
            return;
        }
        if (args.contains ("--self-test-plugin-scan"))
        {
            const int i = args.indexOf ("--self-test-plugin-scan");
            const auto destination = i + 1 < args.size()
                ? juce::File (args[i + 1])
                : juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("halftrack-tests");
            setApplicationReturnValue (runPluginScanTests (destination));
            quit();
            return;
        }
        if (args.contains ("--self-test-export-ui"))
        {
            setApplicationReturnValue (MainComponent::runExportInteractionTests());
            quit();
            return;
        }
        if (args.contains ("--self-test-separation"))
        {
            const int i = args.indexOf ("--self-test-separation");
            setApplicationReturnValue (MainComponent::runSeparationStressTests (juce::File (args[i + 1])));
            quit();
            return;
        }
        if (args.contains ("--self-test-ui"))
        {
            setApplicationReturnValue (MainComponent::runInteractionTests());
            quit();
            return;
        }
        #endif
        if (args.contains ("--self-test-launcher"))
        {
            const int i = args.indexOf ("--self-test-launcher");
            const auto destination = i + 1 < args.size()
                ? juce::File (args[i + 1])
                : juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("halftrack-tests");
            setApplicationReturnValue (runLauncherTests (destination));
            quit();
            return;
        }
        if (args.contains ("--self-test-latency"))
        {
            const int i = args.indexOf ("--self-test-latency");
            const auto destination = i + 1 < args.size()
                ? juce::File (args[i + 1])
                : juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("halftrack-tests");
            setApplicationReturnValue (runLatencyTests (destination));
            quit();
            return;
        }
        // Child process of the Session suite's settings-isolation check.
        if (args.contains ("--self-test-settings-probe"))
        {
            const int i = args.indexOf ("--self-test-settings-probe");
            setApplicationReturnValue (i + 1 < args.size() ? runSettingsIsolationProbe (juce::File (args[i + 1])) : 1);
            quit();
            return;
        }
        if (args.contains ("--self-test-recording"))
        {
            const int i = args.indexOf ("--self-test-recording");
            const auto destination = i + 1 < args.size()
                ? juce::File (args[i + 1])
                : juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("halftrack-tests");
            setApplicationReturnValue (runRecordingTests (destination));
            quit();
            return;
        }
        if (args.contains ("--self-test"))
        {
            const int i = args.indexOf ("--self-test");
            auto destination = i + 1 < args.size()
                ? juce::File (args[i + 1])
                : juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("halftrack-tests");
            int result = runSessionTests (destination);
            if (result == 0) result = runMidiTests (destination);
            if (result == 0) result = runPersistenceTests (destination);
            #if HALFTRACK_TESTS_ENABLED
            if (result == 0) result = runRealtimeTests();
            #endif
            setApplicationReturnValue (result);
            quit();
            return;
        }
        session = std::make_unique<Session>();
        bool opened = false; // A project or files given at launch replace the new project.
        const int demo = args.indexOf ("--demo-dir");
        if (demo >= 0 && demo + 1 < args.size())
        {
            opened = true;
            auto directory = juce::File (args[demo + 1]);
            auto files = directory.findChildFiles (juce::File::findFiles, false, "*.wav");
            std::sort (files.begin(), files.end(), [] (const auto& a, const auto& b)
                { return a.getFileName().compareNatural (b.getFileName()) < 0; });
            int index = 0;
            for (const auto& file : files)
            {
                const auto state = session->snapshot();
                const int target = index < (int) state.tracks.size() ? index : -1;
                auto result = session->importAudio (file, target);
                if (result.failed()) juce::Logger::writeToLog (result.getErrorMessage());
                else
                {
                    session->renameTrack (index, file.getFileNameWithoutExtension());
                    ++index;
                }
            }
        }
        else if (args.size() == 1 && juce::File (args[0]).hasFileExtension ("halftrack"))
        {
            opened = true;
            auto result = session->loadProject (juce::File (args[0]));
            if (result.failed())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                    "Could not open project", result.getErrorMessage());
        }
        // As the reference does, look for new, changed and removed plug-ins at each launch, unless
        // Alt (Option on a Mac) is held.
        if (! juce::ModifierKeys::getCurrentModifiersRealtime().isAltDown())
            if (auto scan = session->rescanPlugins(); scan.failed()) juce::Logger::writeToLog (scan.getErrorMessage());
        window = std::make_unique<Window> (*session);
        window->content->setUserLibrary (juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Halftrack").getChildFile ("User Library"));
        if (! opened) window->content->startNewProject(); // As the reference starts, from the default set.
        window->content->enableRecovery (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                             .getChildFile ("Halftrack").getChildFile ("Recovery").getChildFile ("Recovery.halftrack"));
    }

    void shutdown() override { window.reset(); session.reset(); }
    void systemRequestedQuit() override
    {
        if (window)
            window->content->requestClose ([this] { quit(); });
        else
            quit();
    }
    void anotherInstanceStarted (const juce::String&) override
    {
        if (window) window->toFront (true);
    }
private:
    struct Window final : juce::DocumentWindow
    {
        explicit Window (Session& session)
            : DocumentWindow ("Halftrack", juce::Colour (0xff282a2c), allButtons)
        {
            setUsingNativeTitleBar (true);
            content = new MainComponent (session);
            setContentOwned (content, true);
            setResizable (true, true);
            setResizeLimits (1100, 720, 7680, 4320);
            centreWithSize (1440, 900);
            setVisible (true);
            content->grabKeyboardFocus();
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
        MainComponent* content = nullptr;
    };
    std::unique_ptr<Session> session;
    std::unique_ptr<Window> window;
};
}
START_JUCE_APPLICATION (halftrack::Application)
