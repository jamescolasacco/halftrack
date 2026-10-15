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
int runRealtimeTests();
int runPluginScanTests (const juce::File&);
int runPluginEditorTests (const juce::File&);
int runSendReturnTests (const juce::File&);
#if JUCE_MAC
juce::Result registerAudioUnitTestFixture();
int runAudioUnitTests (const juce::File&);
#endif
#endif
class Application final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Halftrack"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
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
        #if HALFTRACK_TESTS_ENABLED
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
        const int demo = args.indexOf ("--demo-dir");
        if (demo >= 0 && demo + 1 < args.size())
        {
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
            auto result = session->loadProject (juce::File (args[0]));
            if (result.failed())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                    "Could not open project", result.getErrorMessage());
        }
        window = std::make_unique<Window> (*session);
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
