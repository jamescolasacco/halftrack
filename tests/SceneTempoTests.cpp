#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Scene tempo and time signature, through the synthetic device. A scene's tempo and meter are
// optional, each set as one undo step and saved. Launching the scene makes them the project's:
// at once with the transport stopped, otherwise at the scene's launch boundary, where its clips
// start in phase at the new tempo. Stopping before the boundary cancels the change.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS scene tempo: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
constexpr double blockSeconds = 256.0 / 48000;
using Meter = Session::TimeSignature;
}

int runSceneTempoTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("scene-tempo-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        Session s (false, makeSyntheticTestDeviceType());
        const auto scenes = [&] { return s.snapshot().scenes; };
        const auto first = scenes()[0].id, second = scenes()[1].id, third = scenes()[2].id;
        check (! scenes()[0].tempo && ! scenes()[0].timeSignature, "a new scene has no tempo or time signature");

        ok (s.setSceneTempo (second, 140), "give Scene 2 a tempo of 140 BPM");
        ok (s.setSceneTimeSignature (second, Meter { 3, 4 }), "and a 3/4 meter");
        check (scenes()[1].tempo == 140.0 && scenes()[1].timeSignature == Meter { 3, 4 }, "the scene reports them");
        check (s.setSceneTempo (second, 19).failed() && s.setSceneTempo (second, 1000).failed() && s.setSceneTempo (second, std::nan ("")).failed()
               && s.setSceneTimeSignature (second, Meter { 3, 5 }).failed() && s.setSceneTimeSignature (second, Meter { 100, 4 }).failed()
               && s.setSceneTimeSignature (second, Meter { 0, 4 }).failed() && s.setSceneTempo ("missing", 120).failed(),
               "tempos outside 20-999 BPM, meters outside 1-99 over 1, 2, 4, 8 or 16, and unknown scenes are refused");
        s.undo();
        check (! scenes()[1].timeSignature && scenes()[1].tempo == 140.0, "each setting is one undo step");
        s.redo();
        ok (s.setSceneTempo (third, 100), "give Scene 3 a tempo of 100 BPM");
        ok (s.setSceneTempo (third, std::nullopt), "and clear it");
        check (! scenes()[2].tempo, "clearing removes the tempo");
        ok (s.setSceneTempo (third, 100), "set it again");

        // With the transport stopped, the change applies as the scene launches.
        ok (s.launchScene (1), "launch Scene 2 with the transport stopped");
        check (s.snapshot().tempo == 140 && s.snapshot().timeSignature == Meter { 3, 4 }, "its tempo and meter apply at once");
        s.stop();
        s.undo();
        check (s.snapshot().tempo == 120 && s.snapshot().timeSignature == Meter {} && scenes()[1].tempo == 140.0,
               "one undo returns the project's tempo and meter, keeping the scene's");

        // While playing, at the launch boundary, with the scene's clip in phase at the new tempo.
        ok (s.addMidiTrack ("Keys"), "add a MIDI track");
        const auto keys = s.snapshot().tracks.back().id;
        const int keysIndex = (int) s.snapshot().tracks.size() - 1;
        ok (s.addMidiClipToSlot (keys, 1, 4), "put a clip in Scene 2");
        ok (s.setLaunchQuantisation (Session::LaunchQuantisation::bar), "launch on the bar");
        s.seek (0);
        pumpSyntheticTestDevice (s, 8); // The edits' graph rebuild settles first.
        ok (s.launchScene (0), "launch Scene 1, which has no tempo");
        pumpSyntheticTestDevice (s, 8);
        check (s.snapshot().playing && s.snapshot().tempo == 120, "a scene without a tempo leaves the project's");
        while (s.snapshot().position < .5) pumpSyntheticTestDevice (s, 1);
        ok (s.launchScene (1), "launch Scene 2 on beat 2");
        pumpSyntheticTestDevice (s, 8);
        check (s.snapshot().tempo == 120 && s.snapshot().timeSignature == Meter {}, "the change waits for the launch boundary");
        // The scene's clip starts at the boundary, and its launch handle's play position, unlike the
        // snapshot's position, does not trail the engine. The change applies once the engine has processed
        // past the boundary, so in the block after it the clip has played for at most three blocks.
        const auto slot = [&] { return s.snapshot().tracks[(size_t) keysIndex].slots[1]; };
        std::optional<Session::SlotInfo> atChange;
        double previous = s.snapshot().position;
        bool continuous = true;
        for (int block = 0; block < 600 && ! atChange; ++block)
        {
            pumpSyntheticTestDevice (s, 1);
            const auto now = s.snapshot();
            continuous = continuous && now.playing && now.position >= previous - 1e-9;
            previous = now.position;
            if (now.tempo == 140) atChange = now.tracks[(size_t) keysIndex].slots[1];
        }
        check (atChange && s.snapshot().timeSignature == Meter { 3, 4 }, "at the bar the scene's tempo and meter take over");
        pumpSyntheticTestDevice (s, 1);
        const auto started = slot();
        check ((! atChange->playing || atChange->playPositionBeats < 3 * blockSeconds * 2) && started.playing
               && started.playPositionBeats >= 0 && started.playPositionBeats < 3 * blockSeconds * 140 / 60,
               "the change comes as the scene's clip starts (" + juce::String (started.playPositionBeats, 4) + " beats in a block later)");
        check (continuous, "the transport plays on through the change");
        pumpSyntheticTestDevice (s, 4);
        const auto from = slot().playPositionBeats;
        pumpSyntheticTestDevice (s, 120);
        const auto to = slot().playPositionBeats;
        const auto advanced = std::fmod (to - from + 4, 4.0), expected = 120 * blockSeconds * 140 / 60;
        check (slot().playing && from >= 0 && from < .25 && std::abs (advanced - expected) < .03,
               "the scene's clip starts at the boundary and plays at 140 BPM (" + juce::String (advanced, 3) + " of " + juce::String (expected, 3) + " beats)");

        // Stopping before the boundary cancels a waiting change.
        ok (s.launchScene (2), "launch Scene 3 at 100 BPM");
        pumpSyntheticTestDevice (s, 4);
        s.stop();
        pumpSyntheticTestDevice (s, 400);
        check (s.snapshot().tempo == 140, "Stop before the boundary cancels it");

        // Duplicating and reopening.
        juce::String copy;
        ok (s.duplicateScene (second, copy), "duplicate Scene 2");
        check (scenes()[2].id == copy && scenes()[2].tempo == 140.0 && scenes()[2].timeSignature == Meter { 3, 4 }, "the copy keeps the tempo and meter");
        const auto project = root.getChildFile ("Scenes").getChildFile ("Scenes.halftrack");
        ok (s.saveProject (project), "save");
        ok (s.loadProject (project), "reopen");
        check (scenes()[1].tempo == 140.0 && scenes()[1].timeSignature == Meter { 3, 4 } && scenes()[3].tempo == 100.0 && ! scenes()[0].tempo,
               "the scenes reopen with their settings");
        const auto malformed = [&] (const juce::String& name, auto&& change)
        {
            auto xml = juce::XmlDocument::parse (project); check (xml != nullptr, "read the saved project");
            auto* sceneXml = xml->getChildByName ("EDIT")->getChildByName ("SCENES")->getChildElement (1);
            change (*sceneXml);
            const auto file = root.getChildFile (name + ".halftrack"); check (xml->writeTo (file), "write " + name);
            check (s.loadProject (file).failed() && scenes()[1].tempo == 140.0, "a project with " + name + " is refused, keeping the open one");
        };
        malformed ("a 5000 BPM scene", [] (juce::XmlElement& e) { e.setAttribute ("halftrackSceneTempo", 5000); });
        malformed ("a 3-over-5 scene", [] (juce::XmlElement& e) { e.setAttribute ("halftrackSceneDenominator", 5); });
        malformed ("half a scene meter", [] (juce::XmlElement& e) { e.removeAttribute ("halftrackSceneNumerator"); });
        juce::ignoreUnused (first);
        std::cout << "Scene tempo passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL scene tempo: " << e.what() << std::endl; return 1; }
}
}
