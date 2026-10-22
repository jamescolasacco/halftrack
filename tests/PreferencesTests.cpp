#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Preferences: their defaults with no file, a change saved and read by the next Session, the warp
// mode and launch settings they give new clips, Auto-Warp Long Samples, refusals and reset; then
// files from an older version (defaults fill in), a newer one (unknown settings ignored), an
// out-of-range value and an unreadable file (set aside, keeping the defaults, with the reason).
namespace halftrack
{
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS preferences: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
void tone (const juce::File& file, double seconds)
{
    juce::AudioBuffer<float> x (2, (int) (seconds * 8000));
    for (int c = 0; c < 2; ++c) for (int n = 0; n < x.getNumSamples(); ++n) x.setSample (c, n, (float) (.2 * std::sin (n * .1)));
    juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (8000).withNumChannels (2).withBitsPerSample (16));
    check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write " + file.getFileName());
}
}

int runPreferencesTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("preferences-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto shortTone = root.getChildFile ("short.wav"), longTone = root.getChildFile ("long.wav");
        tone (shortTone, 10); tone (longTone, 40);
        juce::File file;
        Session::Preferences chosen;
        chosen.exclusiveArm = false; chosen.defaultWarpMode = Session::WarpMode::beats; chosen.autoWarpLongSamples = true;
        chosen.defaultLaunchMode = Session::LaunchMode::gate; chosen.defaultLaunchQuantisation = Session::ClipQuantisation::bar;
        chosen.startRecordingOnSceneLaunch = true;
        check (! Session::Preferences {}.startRecordingOnSceneLaunch, "Start Recording on Scene Launch is off by default, as in the reference");
        {
            Session s (false);
            file = s.preferencesFile();
            file.deleteFile();
        }
        {
            Session s (false);
            check (s.preferences() == Session::Preferences {} && s.preferencesProblem().isEmpty() && ! file.existsAsFile(), "with no file the defaults apply");
            ok (s.setPreferences (chosen), "change them");
            auto xml = juce::XmlDocument::parse (file);
            check (xml != nullptr && xml->getIntAttribute ("version") == Session::preferencesVersion, "they are saved with their version");
            Session::Preferences bad = chosen; bad.defaultWarpMode = Session::WarpMode::off;
            check (s.setPreferences (bad).failed() && s.preferences() == chosen, "a default warp mode of Off is refused");
        }
        {
            Session s (false);
            check (s.preferences() == chosen, "the next Session reads them");
            // New Session clips take the warp mode and launch settings.
            ok (s.importAudioToSlot (shortTone, 0, 0), "import to a Session slot");
            const auto slot = s.snapshot().tracks[0].slots[0].clip;
            check (slot.warp.mode == Session::WarpMode::beats && slot.launch.mode == Session::LaunchMode::gate && slot.launch.quantisation == Session::ClipQuantisation::bar,
                   "a Session audio clip warps in Beats and launches as Gate on the bar");
            s.undo();
            check (! s.snapshot().tracks[0].slots[0].hasClip, "the import is one undo step");
            ok (s.addMidiTrack ("Keys"), "add a MIDI track");
            const int keys = (int) s.snapshot().tracks.size() - 1;
            ok (s.addMidiClipToSlot (s.snapshot().tracks[(size_t) keys].id, 0, 4), "a Session MIDI clip");
            check (s.snapshot().tracks[(size_t) keys].slots[0].clip.launch.mode == Session::LaunchMode::gate, "it launches as Gate too");
            // Auto-Warp Long Samples: a 40-second sample warps at its estimated tempo; a 10-second one does not.
            ok (s.importAudio (longTone, 0, 0), "import a long sample");
            ok (s.importAudio (shortTone, 0, 60), "and a short one");
            const auto clips = s.snapshot().tracks[0].clips;
            check (clips.size() == 2 && clips[0].warp.mode == Session::WarpMode::beats && std::abs (clips[0].warp.bpm - Session::estimateClipTempo (40)) < 1e-6
                       && clips[1].warp.mode == Session::WarpMode::off, "the long sample is warped in Beats, the short one not");
            ok (s.resetPreferences(), "reset");
            check (s.preferences() == Session::Preferences {}, "back to the defaults");
        }
        {
            Session s (false);
            check (s.preferences() == Session::Preferences {}, "and the defaults stay reset");
        }
        // Files from other versions and damaged files.
        const auto reopen = [&] (const juce::String& text)
        {
            check (file.replaceWithText (text), "write a preferences file");
            Session s (false);
            return std::pair { s.preferences(), s.preferencesProblem() };
        };
        auto [older, olderProblem] = reopen ("<HALFTRACKPREFERENCES version=\"0\" exclusiveSolo=\"0\"/>");
        Session::Preferences expected; expected.exclusiveSolo = false;
        check (older == expected && olderProblem.isEmpty(), "an older file keeps its setting and gains the defaults");
        auto [scrolling, scrollingProblem] = reopen ("<HALFTRACKPREFERENCES version=\"1\" followBehaviour=\"1\"/>");
        expected = {}; expected.scrollFollow = true;
        check (scrolling == expected && scrollingProblem.isEmpty(), "Follow Behavior Scroll is read back");
        auto [countIn, countInProblem] = reopen ("<HALFTRACKPREFERENCES version=\"1\" countInBars=\"4\"/>");
        expected = {}; expected.countInBars = 4;
        check (countIn == expected && countInProblem.isEmpty(), "a four-bar count-in is read back");
        auto [sceneRecording, sceneRecordingProblem] = reopen ("<HALFTRACKPREFERENCES version=\"1\" startRecordingOnSceneLaunch=\"1\"/>");
        expected = {}; expected.startRecordingOnSceneLaunch = true;
        check (sceneRecording == expected && sceneRecordingProblem.isEmpty(), "Start Recording on Scene Launch is read back");
        auto [newer, newerProblem] = reopen ("<HALFTRACKPREFERENCES version=\"7\" exclusiveArm=\"0\" futureSetting=\"42\"/>");
        expected = {}; expected.exclusiveArm = false;
        check (newer == expected && newerProblem.isEmpty(), "a newer file's unknown settings are ignored");
        const auto aside = file.getSiblingFile ("Halftrack Preferences (unreadable).xml");
        aside.deleteFile();
        auto [ranged, rangedProblem] = reopen ("<HALFTRACKPREFERENCES version=\"1\" defaultLaunchMode=\"9\"/>");
        check (ranged == Session::Preferences {} && rangedProblem.isNotEmpty() && aside.existsAsFile(), "an out-of-range value is unreadable: the defaults apply");
        auto [junk, junkProblem] = reopen ("not preferences <<<");
        check (junk == Session::Preferences {} && junkProblem.contains ("could not be read") && aside.loadFileAsString() == "not preferences <<<" && ! file.existsAsFile(),
               "an unreadable file is set aside unchanged, with the reason (" + junkProblem + ")");
        aside.deleteFile();
        std::cout << "Preferences passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL preferences: " << e.what() << std::endl; return 1; }
}
}
