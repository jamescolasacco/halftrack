#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Auto-Arm: selecting a MIDI track arms it, giving it All MIDI Ins and Auto monitoring when it has
// no input, so the computer keyboard plays it at once; selecting another track moves the arm and
// takes back the input it gave; arming by hand, Exclusive Arm off, audio tracks and an All MIDI
// Ins taken by another track each keep it from arming; and none of it is an undo step.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
juce::AudioBuffer<float> captureSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS auto arm: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
float peak (Session& s, int blocks) { const auto x = captureSyntheticTestDevice (s, blocks); return x.getMagnitude (0, 0, x.getNumSamples()); }
}

int runAutoArmTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("auto-arm-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        Session s (false, makeSyntheticTestDeviceType());
        ok (s.saveProject (root.getChildFile ("Arm").getChildFile ("Arm.halftrack")), "save a recording destination");
        for (int t = 0; t < (int) s.snapshot().tracks.size(); ++t) ok (s.setTrackMute (t, true), "mute the audio tracks");
        for (const auto* name : { "Lead", "Bass", "Pads" }) ok (s.addMidiTrack (name), "add a MIDI track");
        const auto index = [&] (const juce::String& name)
        { const auto tracks = s.snapshot().tracks; for (int i = 0; i < (int) tracks.size(); ++i) if (tracks[(size_t) i].name == name) return i; return -1; };
        const auto track = [&] (const juce::String& name) { return s.snapshot().tracks[(size_t) index (name)]; };
        const auto lead = track ("Lead").id, bass = track ("Bass").id, pads = track ("Pads").id, audio = s.snapshot().tracks[0].id;
        Session::SynthSettings sine; sine.waveform = 2; sine.attackSeconds = .002f; sine.sustain = 1; sine.releaseSeconds = .05f; sine.cutoffHz = 20000;
        for (const auto& id : { lead, bass }) ok (s.setSynthSettings (id, sine), "a sine voice");
        ok (s.renameTrack (index ("Pads"), "Pad"), "an edit to undo later");

        ok (s.autoArmTrack (lead), "select Lead");
        auto info = track ("Lead");
        check (info.armed && info.autoArmed && info.inputName == "All MIDI Ins" && info.monitor == Session::MonitorMode::automatic,
               "Lead is auto-armed with All MIDI Ins and Auto monitoring");
        check (s.autoArmedTrack() == lead, "and it is the auto-armed track");

        // The computer keyboard plays it at once.
        ok (s.setComputerKeyboardEnabled (true), "turn the computer keyboard on");
        pumpSyntheticTestDevice (s, 8);
        ok (s.computerKeyboardNote (60, .8f, true), "press a key");
        pumpSyntheticTestDevice (s, 4);
        check (peak (s, 20) > .05f, "the auto-armed track plays it");
        ok (s.computerKeyboardNote (60, 0, false), "release it");
        pumpSyntheticTestDevice (s, 48);

        // Selecting another MIDI track moves the arm and the input it gave.
        ok (s.autoArmTrack (bass), "select Bass");
        check (track ("Bass").autoArmed && track ("Bass").inputName == "All MIDI Ins" && ! track ("Lead").armed && track ("Lead").inputName.isEmpty(),
               "Bass is auto-armed with All MIDI Ins, and Lead has neither");
        s.undo();
        check (index ("Pads") >= 0 && index ("Pad") < 0, "auto-arming is not an undo step: undo reverts the rename");
        check (track ("Bass").autoArmed, "and Bass stays auto-armed");
        s.redo();
        // An audio track releases it.
        ok (s.autoArmTrack (audio), "select an audio track");
        check (! track ("Bass").armed && track ("Bass").inputName.isEmpty() && s.autoArmedTrack().isEmpty() && ! s.snapshot().tracks[0].armed, "an audio track is not auto-armed, and Bass is released");

        // A track with its own input keeps it.
        ok (s.setTrackInput (index ("Lead"), Session::computerKeyboardName), "give Lead the Computer Keyboard");
        ok (s.autoArmTrack (lead), "select Lead");
        check (track ("Lead").autoArmed && track ("Lead").inputName == Session::computerKeyboardName, "Lead is auto-armed on its own input");
        ok (s.autoArmTrack (bass), "select Bass");
        check (! track ("Lead").armed && track ("Lead").inputName == Session::computerKeyboardName, "released, Lead keeps its input");

        // Arming by hand ends auto-arm; another explicitly armed track keeps it off.
        ok (s.setTrackArmed (index ("Bass"), false), "disarm Bass by hand");
        check (! track ("Bass").armed && s.autoArmedTrack().isEmpty(), "Bass is disarmed and no longer auto-armed");
        ok (s.setTrackArmed (index ("Lead"), true), "arm Lead by hand");
        ok (s.autoArmTrack (bass), "select Bass");
        check (! track ("Bass").armed && track ("Lead").armed && ! track ("Lead").autoArmed, "while Lead is armed by hand, Bass is not auto-armed");
        ok (s.setTrackArmed (index ("Lead"), false), "disarm Lead");

        // All MIDI Ins given to another track by hand is not taken.
        ok (s.setTrackInput (index ("Pad"), "All MIDI Ins"), "give Pad All MIDI Ins by hand");
        ok (s.autoArmTrack (bass), "select Bass");
        check (! track ("Bass").armed && track ("Pad").inputName == "All MIDI Ins", "Bass is not auto-armed: Pad keeps All MIDI Ins");
        ok (s.setTrackInput (index ("Pad"), {}), "take it back from Pad");

        // With Exclusive Arm off there is no auto-arm.
        auto preferences = s.preferences(); const auto original = preferences;
        preferences.exclusiveArm = false; ok (s.setPreferences (preferences), "turn Exclusive Arm off");
        ok (s.autoArmTrack (bass), "select Bass");
        check (! track ("Bass").armed, "without Exclusive Arm, selecting a track arms nothing");
        ok (s.setPreferences (original), "restore the preferences");

        // Auto-armed tracks record.
        ok (s.autoArmTrack (bass), "select Bass");
        pumpSyntheticTestDevice (s, 8);
        s.seek (0); ok (s.record(), "record");
        pumpSyntheticTestDevice (s, 24);
        ok (s.computerKeyboardNote (64, .8f, true), "play E3");
        pumpSyntheticTestDevice (s, 48);
        ok (s.computerKeyboardNote (64, 0, false), "release it");
        pumpSyntheticTestDevice (s, 24);
        s.stop(); pumpSyntheticTestDevice (s, 8);
        const auto clips = track ("Bass").clips;
        check (clips.size() == 1 && clips[0].notes.size() == 1 && clips[0].notes[0].pitch == 64, "the auto-armed track records the note");
        check (s.autoArmTrack (lead).wasOk() && track ("Lead").autoArmed, "after recording, selecting moves the arm again");
        ok (s.setComputerKeyboardEnabled (false), "turn the keyboard off");
        std::cout << "Auto arm passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL auto arm: " << e.what() << std::endl; return 1; }
}
}
