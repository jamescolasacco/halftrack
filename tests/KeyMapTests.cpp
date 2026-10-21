#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Mapped buttons and Key Map Mode: which targets are buttons; learning a key (one undo step);
// keys pressing a track's activator, solo, arm and clip stop, a device's switch, clip slots,
// scenes and the transport; a key switching a control between its ends; one key per target and
// one target per key; unmapped and unprintable keys left alone; mappings removed, hidden with
// their target, reopened and refused when damaged; and a controller pressing a button as its value
// rises above 63.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS key map: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
// Maps a key to a target, as Key Map Mode does.
void learn (Session& s, const juce::String& target, juce::juce_wchar key)
{
    ok (s.setKeyLearnTarget (target), "learn " + target);
    bool used = false;
    ok (s.handleMappedKey (key, used), "press " + juce::String::charToString (key));
    check (used, "the key is taken while learning");
    ok (s.setKeyLearnTarget ({}), "leave learning");
}
bool press (Session& s, juce::juce_wchar key)
{
    bool used = false;
    ok (s.handleMappedKey (key, used), "press " + juce::String::charToString (key));
    return used;
}
juce::juce_wchar keyOf (const Session& s, const juce::String& target)
{ for (const auto& m : s.snapshot().keyMappings) if (m.target == target) return m.key; return 0; }
}

int runKeyMapTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("key-map-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        Session s (false, makeSyntheticTestDeviceType());
        ok (s.saveProject (root.getChildFile ("Keys").getChildFile ("Keys.halftrack")), "save a recording destination");
        const auto track = [&] (int i) { return s.snapshot().tracks[(size_t) i]; };
        const auto audio = track (0).id;
        const auto scene = s.snapshot().scenes[0].id;
        check (Session::isButtonTarget ("track:x:mute") && Session::isButtonTarget ("transport:play") && Session::isButtonTarget ("device:x:on")
                   && ! Session::isButtonTarget ("device:x:1") && ! Session::isButtonTarget ("mixer:x:volume") && ! Session::isButtonTarget ("master"),
               "switches, launches and the transport are buttons; faders and knobs are not");
        check (s.isMappableTarget ("track:" + audio + ":mute") && s.isMappableTarget ("scene:" + scene) && s.isMappableTarget ("transport:metronome")
                   && ! s.isMappableTarget ("track:missing:mute") && ! s.isMappableTarget ("transport:rewind") && ! s.isMappableTarget ("scene:missing")
                   && s.setKeyLearnTarget ("track:missing:solo").failed(), "only targets that exist can be mapped");

        // Learning is one undo step.
        const auto mute = "track:" + audio + ":mute";
        learn (s, mute, 'Q');
        check (keyOf (s, mute) == 'q' && s.snapshot().keyLearnTarget.isEmpty(), "Q maps to the activator, without case");
        s.undo();
        check (keyOf (s, mute) == 0, "undo removes the mapping");
        s.redo();
        check (keyOf (s, mute) == 'q', "redo restores it");

        // Switches.
        check (press (s, 'q') && track (0).mute, "Q switches the track off");
        check (press (s, 'Q') && ! track (0).mute, "and on again");
        learn (s, "track:" + audio + ":solo", 'w');
        check (press (s, 'w') && track (0).solo && press (s, 'w') && ! track (0).solo, "W solos and unsolos the track");
        ok (s.setTrackInput (0, s.inputNames()[0]), "give the track an input");
        learn (s, "track:" + audio + ":arm", 'e');
        check (press (s, 'e') && track (0).armed && press (s, 'e') && ! track (0).armed, "E arms and disarms it");
        ok (s.addNativeEffect (0, Session::EffectKind::eqThree), "add an EQ Three");
        const auto eq = track (0).plugins.back().id;
        learn (s, "device:" + eq + ":on", 'r');
        check (press (s, 'r') && ! track (0).plugins.back().enabled && press (s, 'r') && track (0).plugins.back().enabled, "R switches the EQ off and on");

        // A key on a control switches it between its ends.
        learn (s, "mixer:" + audio + ":volume", 't');
        check (press (s, 't') && track (0).gainDb <= -99.9f, "T sends the volume to its minimum, the end it is further from");
        check (press (s, 't') && std::abs (track (0).gainDb - 6) < 1e-3f, "and then to its maximum");

        // The transport.
        learn (s, "transport:play", 'p'); learn (s, "transport:stop", 'o'); learn (s, "transport:loop", 'l'); learn (s, "transport:metronome", 'k');
        check (press (s, 'p') && s.snapshot().playing, "P plays");
        check (press (s, 'p') && s.snapshot().playing, "and pressed again keeps playing");
        check (press (s, 'o') && ! s.snapshot().playing, "O stops");
        const bool looping = s.snapshot().looping, clicking = s.snapshot().metronome.enabled;
        check (press (s, 'l') && s.snapshot().looping != looping && press (s, 'k') && s.snapshot().metronome.enabled != clicking, "L switches the loop and K the metronome");
        press (s, 'l'); press (s, 'k');

        // Clip slots, scenes and clip stop.
        ok (s.addMidiTrack ("Keys"), "add a MIDI track");
        const int keys = (int) s.snapshot().tracks.size() - 1;
        const auto keysId = track (keys).id;
        ok (s.addMidiClipToSlot (keysId, 0, 4), "a Session clip in the first scene");
        learn (s, "slot:" + keysId + ":" + scene, 'j');
        learn (s, "track:" + keysId + ":stop", 'g');
        learn (s, "scene:" + scene, 'h');
        const auto settle = [&] (bool playing) { for (int i = 0; i < 2000 && track (keys).slots[0].playing != playing; ++i) pumpSyntheticTestDevice (s, 1); return track (keys).slots[0].playing == playing; };
        check (press (s, 'j') && settle (true), "J launches the clip");
        check (press (s, 'g') && settle (false), "G stops the track's clip");
        check (press (s, 'h') && settle (true), "H launches the scene");
        press (s, 'g'); settle (false); s.stop();

        // One key per target, one target per key; other keys are left alone.
        learn (s, "track:" + audio + ":solo", 'q');
        check (keyOf (s, "track:" + audio + ":solo") == 'q' && keyOf (s, mute) == 0, "mapping Q to Solo takes it from the activator");
        learn (s, "track:" + audio + ":solo", 'z');
        check (keyOf (s, "track:" + audio + ":solo") == 'z' && ! press (s, 'q'), "a target keeps one key: Q is free again");
        check (! press (s, 'x') && ! press (s, ' ') && ! press (s, '\t'), "unmapped keys, spaces and unprintable keys are left alone");
        ok (s.removeKeyMapping ("track:" + audio + ":solo"), "remove Solo's mapping");
        check (keyOf (s, "track:" + audio + ":solo") == 0 && ! press (s, 'z') && s.removeKeyMapping ("track:" + audio + ":solo").failed(), "Z is free and Solo has no mapping");

        // A mapping whose target goes is hidden; mappings reopen; damaged ones are refused.
        ok (s.removePlugin (0, (int) track (0).plugins.size() - 1), "delete the EQ");
        check (keyOf (s, "device:" + eq + ":on") == 0 && ! press (s, 'r'), "the EQ's mapping is hidden and R does nothing");
        s.undo();
        check (keyOf (s, "device:" + eq + ":on") == 'r', "undo brings it back");
        const auto project = root.getChildFile ("Reopen").getChildFile ("Reopen.halftrack");
        ok (s.saveProject (project), "save");
        const auto saved = s.snapshot().keyMappings;
        {
            Session reopened (false);
            ok (reopened.loadProject (project), "reopen");
            check (reopened.snapshot().keyMappings == saved && saved.size() >= 9, "every key mapping reopens");
            const auto damaged = root.getChildFile ("Damaged.halftrack");
            check (damaged.replaceWithText (project.loadFileAsString().replace ("key=\"p\"", "key=\"pp\"")), "write a project with a two-letter key");
            check (reopened.loadProject (damaged).failed() && reopened.snapshot().keyMappings == saved, "it is refused, keeping the open project");
        }

        // A controller presses a button as its value rises above 63.
        ok (s.setMidiLearnTarget (mute), "learn the activator for a controller");
        ok (s.handleMidiControl (1, 40, 127), "move controller 40");
        ok (s.setMidiLearnTarget ({}), "leave learning");
        check (std::any_of (s.snapshot().midiMappings.begin(), s.snapshot().midiMappings.end(), [&] (const auto& m) { return m.target == mute && m.controller == 40; }),
               "the button has a controller mapping");
        const bool before = track (0).mute;
        ok (s.handleMidiControl (1, 40, 127), "press");
        check (track (0).mute != before, "rising above 63 presses it");
        ok (s.handleMidiControl (1, 40, 120), "stay high");
        check (track (0).mute != before, "staying above 63 does not press again");
        ok (s.handleMidiControl (1, 40, 0), "release");
        ok (s.handleMidiControl (1, 40, 64), "press again");
        check (track (0).mute == before, "rising again presses it again");
        check (s.setMidiMappingRange (mute, 0, 1).failed(), "a button has no range to set");
        std::cout << "Key map passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL key map: " << e.what() << std::endl; return 1; }
}
}
