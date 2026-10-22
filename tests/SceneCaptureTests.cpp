#include "Session.h"
#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_engine/testing/tracktion_EnginePlayer.h>
#include <iostream>
#include <stdexcept>

// Capture and Insert Scene: two MIDI tracks play clips from different scenes, and capturing makes a
// scene after the selected one holding copies of exactly those clips, with their own notes, which
// launches to play them. Nothing playing, nothing is captured; the capture is one undo step.
namespace halftrack
{
namespace te = tracktion::engine;
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS scene capture: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
}

int runSceneCaptureTests (const juce::File&)
{
    try
    {
        Session s (false);
        auto& engine = dynamic_cast<te::DeviceManager::TracktionEngineAudioDeviceManager&> (s.audioDeviceManager()).engine;
        te::HostedAudioDeviceInterface::Parameters p; p.sampleRate = 48000; p.blockSize = 128; p.inputChannels = 0; p.outputChannels = 2; p.useMidiDevices = false;
        te::test_utilities::EnginePlayer player (engine, p);
        const auto dispatch = [&] { for (auto* edit : engine.getActiveEdits().getEdits()) edit->dispatchPendingUpdatesSynchronously(); };
        ok (s.setLaunchQuantisation (Session::LaunchQuantisation::none), "launch at once");
        while (s.snapshot().scenes.size() < 3) ok (s.addScene(), "a scene");
        ok (s.addMidiTrack ("Bass"), "a bass track"); ok (s.addMidiTrack ("Keys"), "a keys track");
        const int bass = (int) s.snapshot().tracks.size() - 2, keys = bass + 1;
        // Bass has a clip in scenes 1 and 2, keys in scenes 1 and 2; each holds a note of its own pitch.
        juce::StringArray added;
        for (const auto& [track, scene, pitch] : { std::tuple { bass, 0, 36 }, std::tuple { bass, 1, 38 }, std::tuple { keys, 0, 60 }, std::tuple { keys, 1, 64 } })
        {
            ok (s.addMidiClipToSlot (s.snapshot().tracks[(size_t) track].id, scene, 4), "a clip");
            ok (s.addMidiNotes (s.snapshot().tracks[(size_t) track].slots[(size_t) scene].clip.id, { { "", pitch, 0, 1, .8f } }, added), "its note");
        }
        juce::String captured;
        check (s.captureAndInsertScene (1, captured).failed(), "with nothing playing, nothing is captured");
        ok (s.launchSlot (bass, 0), "play the first bass clip"); ok (s.launchSlot (keys, 1), "and the second keys clip");
        dispatch(); player.process (4096); dispatch();
        const auto scenes = s.snapshot().scenes.size();
        ok (s.captureAndInsertScene (1, captured), "Capture and Insert Scene after the second scene");
        const auto now = s.snapshot();
        check (now.scenes.size() == scenes + 1 && now.scenes[2].id == captured, "a scene is inserted after the second");
        const auto& bassCopy = now.tracks[(size_t) bass].slots[2].clip;
        const auto& keysCopy = now.tracks[(size_t) keys].slots[2].clip;
        check (now.tracks[(size_t) bass].slots[2].hasClip && now.tracks[(size_t) keys].slots[2].hasClip
                   && bassCopy.notes.size() == 1 && bassCopy.notes[0].pitch == 36 && keysCopy.notes.size() == 1 && keysCopy.notes[0].pitch == 64,
               "it holds copies of the playing clips: the first bass clip and the second keys clip");
        check (bassCopy.id != now.tracks[(size_t) bass].slots[0].clip.id && bassCopy.notes[0].id != now.tracks[(size_t) bass].slots[0].clip.notes[0].id,
               "the copies are clips of their own");
        ok (s.launchScene (2), "launch the captured scene");
        dispatch(); player.process (4096); dispatch();
        check (s.snapshot().tracks[(size_t) bass].slots[2].playing && s.snapshot().tracks[(size_t) keys].slots[2].playing, "it plays both copies");
        s.stop(); dispatch();
        s.undo();
        check (s.snapshot().scenes.size() == scenes && s.snapshot().tracks[(size_t) keys].slots[1].clip.notes.size() == 1, "one undo removes the captured scene, leaving the rest");
        std::cout << "Scene capture passed" << std::endl;
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL scene capture: " << e.what() << std::endl; return 1; }
}
}
