#include "Session.h"
#include <iostream>
#include <stdexcept>

// Automatic track colours, as the reference assigns them (static reading of its code; see the track colours
// contract): a project's colour sequence index picks one of eight sequences of palette indices, and two
// pickers step through it from a random slot, forward for audio, MIDI and group tracks and backward for
// returns and the Main. A new project draws both and recolours every track; grouped tracks take their
// group's colour, a new group its first track's, and Auto-Assign Track Colors off gives every new track
// Default Track Color.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS track colours: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
juce::Colour palette (int index) { return Session::paletteColour (index); }
const Session::TrackInfo& named (const Session::Snapshot& s, const juce::String& name)
{
    for (const auto& t : s.tracks) if (t.name == name) return t;
    throw std::runtime_error (("no track named " + name).toStdString());
}
// The track in `after` that `before` did not have.
Session::TrackInfo added (const Session::Snapshot& before, const Session::Snapshot& after)
{
    for (const auto& t : after.tracks)
        if (std::none_of (before.tracks.begin(), before.tracks.end(), [&] (const auto& old) { return old.id == t.id; })) return t;
    throw std::runtime_error ("no track was added");
}
}

int runTrackColourTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("track-colours-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        Session s (false, makeSyntheticTestDeviceType());
        const auto defaults = s.preferences();
        check (defaults.autoAssignTrackColours && defaults.defaultTrackColour == 0 && ! defaults.reducedAutomaticColours && ! defaults.randomClipColours,
               "Auto-Assign Track Colors is on, Default Track Color the first swatch, and Reduced Automatic Colors and random clip colours off, as the reference's");
        check (Session::automaticColours (1153618690, false) == std::vector<int> { 14, 1, 4, 19, 18, 20, 21, 6, 17, 16, 3, 2, 0, 25, 10, 12, 26 }
                   && Session::automaticColours (7, true) == std::vector<int> { 0, 1, 3, 5, 9, 10, 14, 20, 21, 23, 26 },
               "a sequence index chooses one of the reference's sequences, and Reduced Automatic Colors its 11");

        // The reference's built-in default Set stores colour sequence index 1153618690, tracks coloured 19, 18, 20
        // and 21, the Main 4 and returns 1 and 14, and pickers at 7 and 16. Seeded so its generator draws that
        // index and then a start of 3 (2076818280 mod 17), Halftrack's like project takes those palette places.
        Session::seedColourGenerator (527074069);
        ok (s.newBuiltInDefaultProject(), "a new project like the reference's default Set");
        auto snap = s.snapshot();
        check (snap.colourSequence == 1153618690 && snap.nextTrackColour == 7 && snap.nextReturnColour == 16, "it draws the colour sequence index, and the pickers move on to 7 and 16");
        check (named (snap, "1-MIDI").colour == palette (19) && named (snap, "2-MIDI").colour == palette (18) && named (snap, "3-Audio").colour == palette (20)
                   && named (snap, "4-Audio").colour == palette (21), "the tracks take slots 3 to 6 in order: palette places 19, 18, 20 and 21");
        check (snap.mainColour == palette (4) && snap.tracks[4].colour == palette (1) && snap.tracks[5].colour == palette (14),
               "the Main, then the returns, take slots 2, 1 and 0: palette places 4, 1 and 14");
        check (! snap.dirty && ! s.canUndo(), "the new project is unchanged, without history");
        ok (s.setMainColour (palette (40)), "colour the Main");
        check (s.snapshot().mainColour == palette (40) && s.undoSteps()[0] == "Set track colour", "the Main takes the colour, one undo step");
        s.undo();
        check (s.snapshot().mainColour == palette (4), "undo gives it back its own");

        // New tracks step on: the track picker forward, the return picker back.
        ok (s.addAudioTrack(), "insert an audio track");
        ok (s.addMidiTrack(), "and a MIDI track");
        snap = s.snapshot();
        const auto audio = snap.tracks[4], midi = snap.tracks[5];
        check (audio.colour == palette (6) && midi.colour == palette (17) && snap.nextTrackColour == 9, "they take slots 7 and 8, palette places 6 and 17");
        ok (s.addReturnTrack(), "insert a return track");
        snap = s.snapshot();
        check (snap.tracks.back().colour == palette (26) && snap.nextReturnColour == 15, "a return takes slot 16, palette place 26, its picker stepping back");
        s.undo(); snap = s.snapshot();
        check (snap.nextReturnColour == 16 && snap.nextTrackColour == 9, "undo puts the picker back with the track");

        // A new group takes its first track's colour; a track inserted into it takes the group's, and neither steps the picker.
        juce::String groupId;
        ok (s.groupTracks ({ audio.id, midi.id }, groupId), "group the two new tracks");
        snap = s.snapshot();
        check (snap.groups.size() == 1 && snap.groups[0].colour == audio.colour && snap.nextTrackColour == 9, "the group takes its first track's colour");
        auto previous = snap;
        ok (s.addAudioTrack ({}, audio.id), "insert a track after one in the group");
        snap = s.snapshot();
        const auto inside = added (previous, snap);
        check (inside.groupId == groupId && inside.colour == snap.groups[0].colour && snap.nextTrackColour == 9, "it takes the group's colour, the picker staying");

        // Auto-Assign Track Colors off: every new track, and a new group, takes Default Track Color.
        auto prefs = defaults; prefs.autoAssignTrackColours = false; prefs.defaultTrackColour = 30;
        ok (s.setPreferences (prefs), "switch Auto-Assign Track Colors off, with the default colour at place 30");
        previous = s.snapshot();
        ok (s.addAudioTrack(), "insert an audio track");
        const auto plain = added (previous, s.snapshot());
        ok (s.addReturnTrack(), "and a return");
        snap = s.snapshot();
        check (plain.colour == palette (30) && snap.tracks.back().colour == palette (30) && snap.nextTrackColour == 9 && snap.nextReturnColour == 16,
               "both take Default Track Color, the pickers staying");
        juce::String plainGroup;
        ok (s.groupTracks ({ plain.id }, plainGroup), "group that track");
        snap = s.snapshot();
        check (std::any_of (snap.groups.begin(), snap.groups.end(), [&] (const auto& g) { return g.id == plainGroup && g.colour == palette (30); }), "the new group takes it too");

        // Reduced Automatic Colors: the pickers start again in the 11 colourblind-friendly colours, nothing recoloured.
        prefs = defaults; prefs.reducedAutomaticColours = true;
        const auto coloursBefore = snap.tracks;
        ok (s.setPreferences (prefs), "switch Auto-Assign back on, with Reduced Automatic Colors");
        snap = s.snapshot();
        check (snap.colourSequence == 1153618690 && juce::isPositiveAndBelow (snap.nextTrackColour, 11) && snap.nextReturnColour == (snap.nextTrackColour + 10) % 11,
               "the pickers start again at a random slot of the 11, the return picker one before, the sequence index kept");
        bool kept = coloursBefore.size() == snap.tracks.size();
        for (size_t i = 0; kept && i < snap.tracks.size(); ++i) kept = snap.tracks[i].colour == coloursBefore[i].colour;
        check (kept, "no track is recoloured");
        const auto reducedSlot = snap.nextTrackColour;
        previous = snap;
        ok (s.addMidiTrack(), "insert a MIDI track");
        check (added (previous, s.snapshot()).colour == palette (Session::automaticColours (0, true)[(size_t) reducedSlot]), "a new track takes the reduced sequence's next colour");
        ok (s.setPreferences (defaults), "back to the default colour settings");

        // The project keeps its colour sequence, pickers and Main colour.
        const auto file = root.getChildFile ("Colours").getChildFile ("Colours.halftrack");
        ok (s.saveProject (file), "save the project");
        const auto saved = s.snapshot();
        ok (s.newProject(), "a new project");
        ok (s.loadProject (file), "reopen the project");
        snap = s.snapshot();
        check (snap.colourSequence == saved.colourSequence && snap.nextTrackColour == saved.nextTrackColour && snap.nextReturnColour == saved.nextReturnColour
                   && snap.mainColour == saved.mainColour && snap.tracks[0].colour == saved.tracks[0].colour, "reopened, its colours and pickers are as saved");

        // A plain new project: its two audio tracks take the first two slots from the start, the Main the slot before.
        ok (s.newProject(), "a plain new project");
        snap = s.snapshot();
        const auto sequence = Session::automaticColours (snap.colourSequence, false);
        const int start = (snap.nextTrackColour + 15) % 17;
        check (snap.tracks.size() == 2 && snap.tracks[0].colour == palette (sequence[(size_t) start]) && snap.tracks[1].colour == palette (sequence[(size_t) (start + 1) % 17])
                   && snap.mainColour == palette (sequence[(size_t) (start + 16) % 17]) && snap.nextReturnColour == (start + 15) % 17,
               "its tracks step from a random start, and the Main takes the slot before it");

        // A project whose automatic colours are out of range is refused, keeping the open one.
        auto text = file.loadFileAsString();
        const auto marker = juce::String ("halftrackNextTrackColour=\"");
        const auto at = text.indexOf (marker);
        check (at > 0, "the saved project holds the track picker's place");
        const auto end = text.indexOf (at + marker.length(), "\"");
        const auto broken = root.getChildFile ("Broken.halftrack");
        ok (broken.replaceWithText (text.replaceSection (at + marker.length(), end - at - marker.length(), "17")) ? juce::Result::ok() : juce::Result::fail ("write"), "write a project with the picker past its sequence");
        const auto before = s.snapshot().projectGeneration;
        check (s.loadProject (broken).failed() && s.snapshot().projectGeneration == before, "it is refused, the open project kept");

        // The Clip Color toggle at Random: new clips take colours from the sequence at random, never one of the two
        // before; takes recorded together share one.
        ok (s.newProject(), "a new project for clip colours");
        ok (s.addMidiTrack(), "a MIDI track");
        auto random = defaults; random.randomClipColours = true;
        ok (s.setPreferences (random), "Clip Color at Random");
        const auto midiTrack = s.snapshot().tracks.back();
        for (int i = 0; i < 8; ++i) ok (s.addMidiClip (midiTrack.id, i * 4.0, 2), "add MIDI clip " + juce::String (i + 1));
        snap = s.snapshot();
        std::vector<juce::Colour> allowed;
        for (const auto place : Session::automaticColours (snap.colourSequence, false)) allowed.push_back (palette (place));
        const auto& clips = snap.tracks.back().clips;
        bool fromSequence = clips.size() == 8, freshEachTime = true;
        for (size_t i = 0; i < clips.size(); ++i)
        {
            fromSequence = fromSequence && std::find (allowed.begin(), allowed.end(), clips[i].colour) != allowed.end();
            if (i >= 1) freshEachTime = freshEachTime && clips[i].colour != clips[i - 1].colour;
            if (i >= 2) freshEachTime = freshEachTime && clips[i].colour != clips[i - 2].colour;
        }
        check (fromSequence && freshEachTime, "each new clip takes a colour of the sequence, other than the two before");
        ok (s.saveProject (root.getChildFile ("Takes").getChildFile ("Takes.halftrack")), "save a recording destination");
        ok (s.setTrackInput (0, s.inputNames()[0]), "route the input to the first track");
        ok (s.setTrackInputTrack (s.snapshot().tracks[1].id, s.snapshot().tracks[0].id), "and the first track's audio to the second");
        ok (s.setTrackArmed (0, true), "arm the first"); ok (s.setTrackArmed (1, true), "and the second");
        pumpSyntheticTestDevice (s, 8);
        const auto recordPass = [&]
        {
            s.seek (40); ok (s.record(), "record both tracks");
            pumpSyntheticTestDevice (s, 96);
            s.stop();
            const auto tracks = s.snapshot().tracks;
            check (tracks[0].clips.size() == 1 && tracks[1].clips.size() == 1,
                   "a take on each (" + juce::String ((int) tracks[0].clips.size()) + ", " + juce::String ((int) tracks[1].clips.size()) + " clips; armed "
                       + juce::String ((int) tracks[0].armed) + juce::String ((int) tracks[1].armed) + ", inputs " + tracks[0].inputName + " / " + tracks[1].inputName + ")");
            return std::pair { tracks[0].clips[0].colour, tracks[1].clips[0].colour };
        };
        const auto first = recordPass();
        snap = s.snapshot();
        check (first.first == first.second && std::find (allowed.begin(), allowed.end(), first.first) != allowed.end()
                   && (first.first != snap.tracks[0].colour || first.second != snap.tracks[1].colour), "the takes recorded together share one colour of their own");
        s.undo();
        const auto second = recordPass();
        check (second.first == second.second && second.first != first.first, "the next pass's takes share another");
        ok (s.setPreferences (defaults), "Clip Color back at Track Color");
        ok (s.addMidiClip (midiTrack.id, 64, 2), "add a MIDI clip");
        snap = s.snapshot();
        check (snap.tracks.back().clips.back().colour == snap.tracks.back().colour, "a new clip then shows its track's colour");
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cout << "FAIL track colours: " << e.what() << std::endl;
        return 1;
    }
}
}
