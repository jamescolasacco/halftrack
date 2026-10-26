#include "NoteTools.h"
#include "Session.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <tuple>

// Same-pitch note overlaps. Notes added or edited win over the notes of their pitch they overlap, as the reference's
// do (static reading; docs/research/note-overlaps-contract.md). Every expected result is worked out by hand.
namespace halftrack
{
namespace
{
using Note = Session::MidiNoteInfo;
using Shape = std::tuple<int, double, double>; // Pitch, start and length.
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS note overlaps: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
Note note (int pitch, double start, double length) { return { {}, pitch, start, length, .8f, true }; }
Note moved (const juce::String& id, int pitch, double start, double length) { auto n = note (pitch, start, length); n.id = id; return n; }
std::vector<Shape> shapes (const std::vector<Note>& notes)
{
    std::vector<Shape> result;
    for (const auto& n : notes) result.emplace_back (n.pitch, n.startBeat, n.lengthBeats);
    std::sort (result.begin(), result.end());
    return result;
}
bool same (const std::vector<Shape>& actual, std::vector<Shape> expected)
{
    std::sort (expected.begin(), expected.end());
    if (actual.size() != expected.size()) return false;
    for (size_t i = 0; i < actual.size(); ++i)
        if (std::get<0> (actual[i]) != std::get<0> (expected[i]) || std::abs (std::get<1> (actual[i]) - std::get<1> (expected[i])) > 1e-9
            || std::abs (std::get<2> (actual[i]) - std::get<2> (expected[i])) > 1e-9) return false;
    return true;
}
juce::String describe (const std::vector<Shape>& shapes)
{ juce::String s; for (const auto& [pitch, start, length] : shapes) s << pitch << "@" << start << "+" << length << " "; return s.trim(); }

struct Fixture
{
    Session s { false };
    // A new 16-beat clip on a track of its own, holding the given notes, which nothing selected then holds floating.
    juce::String clip (const std::vector<Note>& ground, juce::StringArray& ids)
    {
        ok (s.addMidiTrack ("Case"), "add a track for the case");
        const auto track = s.snapshot().tracks.back().id;
        ok (s.addMidiClip (track, 0, 16), "a 16-beat clip");
        const auto id = s.snapshot().tracks.back().clips[0].id;
        if (! ground.empty()) ok (s.addMidiNotes (id, ground, ids), "its notes");
        s.setSelectedNotes (id, {});
        return id;
    }
    std::vector<Note> notes (const juce::String& clipId) const
    {
        const auto snapshot = s.snapshot();
        for (const auto& t : snapshot.tracks) for (const auto& c : t.clips) if (c.id == clipId) return c.notes;
        throw std::runtime_error ("Fixture clip missing");
    }
    void expect (const juce::String& clipId, const std::vector<Shape>& expected, const juce::String& text) const
    { const auto actual = shapes (notes (clipId)); check (same (actual, expected), text + " (" + describe (actual) + ")"); }
    bool holds (const juce::String& clipId, const juce::String& noteId) const
    { const auto all = notes (clipId); return std::any_of (all.begin(), all.end(), [&] (const Note& n) { return n.id == noteId; }); }
};
}

int runNoteOverlapTests (const juce::File&)
{
    try
    {
        Fixture f;
        // The rule, for notes added.
        const auto added = [&] (const std::vector<Note>& ground, const std::vector<Note>& notes, const std::vector<Shape>& expected, const juce::String& text)
        {
            juce::StringArray ids, newIds;
            const auto clip = f.clip (ground, ids);
            ok (f.s.addMidiNotes (clip, notes, newIds), "add notes: " + text);
            f.expect (clip, expected, text);
            return clip;
        };
        const auto cut = added ({ note (60, 0, 2) }, { note (60, 1, 2) }, { { 60, 0, 1 }, { 60, 1, 2 } },
                                "a note starting before an added one and sounding past its start ends there, losing its tail");
        f.s.undo();
        f.expect (cut, { { 60, 0, 2 } }, "one undo takes the added note back and gives the other its length again");
        added ({ note (60, 1, 1) }, { note (60, 0, 3) }, { { 60, 0, 3 } }, "a note starting inside an added one goes");
        added ({ note (60, 2, 3) }, { note (60, 0, 3) }, { { 60, 0, 3 } }, "even when it sounds on past the added one's end");
        added ({ note (60, 0, 4) }, { note (60, 1, 1) }, { { 60, 0, 1 }, { 60, 1, 1 } }, "a note holding an added one is cut at its start, not split around it");
        added ({ note (60, 1, 1) }, { note (60, 1, .5) }, { { 60, 1, .5 } }, "a note starting with an added one goes, whatever their lengths");
        added ({ note (60, 0, 1), note (60, 2, 1) }, { note (60, 1, 1) }, { { 60, 0, 1 }, { 60, 1, 1 }, { 60, 2, 1 } }, "notes that only touch an added one stay");
        added ({ note (62, 0, 4), note (59, 1, 1) }, { note (60, 1, 1) }, { { 59, 1, 1 }, { 60, 1, 1 }, { 62, 0, 4 } }, "notes of other pitches stay");
        added ({ note (60, 0, 4) }, { note (60, 1, 1), note (60, 2, 1) }, { { 60, 0, 1 }, { 60, 1, 1 }, { 60, 2, 1 } }, "of several notes added, the first a note overlaps cuts it");
        added ({ note (60, 1.5, 1) }, { note (60, 1, 1), note (60, 2, 1) }, { { 60, 1, 1 }, { 60, 2, 1 } }, "and one starting inside the first goes");
        added ({ note (60, 0, 2) }, { note (60, .0005, 1) }, { { 60, .0005, 1 } }, "a note that would end less than a 960th of a beat after its start goes instead");
        {
            juce::StringArray ids, newIds;
            const auto clip = f.clip ({ note (60, 0, 2) }, ids);
            ok (f.s.setMidiNotesActive (clip, ids, false), "deactivate a note");
            ok (f.s.addMidiNotes (clip, { note (60, 1, 2) }, newIds), "add one over it");
            const auto all = f.notes (clip);
            const auto old = std::find_if (all.begin(), all.end(), [&] (const Note& n) { return n.id == ids[0]; });
            check (old != all.end() && ! old->active && std::abs (old->lengthBeats - 1) < 1e-9, "a deactivated note is cut like any other, staying deactivated");
            juce::StringArray copies;
            const auto copied = f.clip ({ note (60, 0, 2) }, ids);
            ok (f.s.addMidiNotes (copied, { note (60, 0, 2) }, copies, false), "copy a note where it is, as a copy drag does");
            f.expect (copied, { { 60, 0, 2 }, { 60, 0, 2 } }, "a copy left where its note is keeps out of the way");
            ok (f.s.updateMidiNotes (copied, { moved (ids[0], 60, .5, 2) }), "drag the note on a little");
            f.expect (copied, { { 60, 0, .5 }, { 60, .5, 2 } }, "until the note moves over it, cutting it short");
            ok (f.s.updateMidiNotes (copied, { moved (ids[0], 60, 3, 2) }), "drag the note off it");
            f.expect (copied, { { 60, 0, 2 }, { 60, 3, 2 } }, "moved off it, the note leaves the copy whole again");
            ok (f.s.updateMidiNotes (copied, { moved (ids[0], 60, 0, 2) }), "drag the note back where it was");
            f.expect (copied, { { 60, 0, 2 } }, "dropped back where it was, the note takes its copy's place");
        }

        // Floating notes: while the same notes are edited, what they cut short or took away comes back first.
        const auto dragCase = [&] (juce::StringArray& ground, juce::StringArray& mine)
        {
            const auto clip = f.clip ({ note (60, 2, 1) }, ground);
            ok (f.s.addMidiNotes (clip, { note (60, 0, 1) }, mine), "a note to drag");
            f.s.setSelectedNotes (clip, mine); // The editor selects the note it added.
            return clip;
        };
        {
            juce::StringArray ground, mine;
            const auto clip = dragCase (ground, mine);
            const auto move = [&] (double start) { ok (f.s.updateMidiNotes (clip, { moved (mine[0], 60, start, 1) }), "move the note to " + juce::String (start)); };
            move (1.5); f.expect (clip, { { 60, 1.5, 1 } }, "dragged over a later note of its pitch, a note takes it away");
            move (4); f.expect (clip, { { 60, 2, 1 }, { 60, 4, 1 } }, "dragged on past it, the note comes back as it was");
            check (f.holds (clip, ground[0]), "with its identity");
            move (2.5); f.expect (clip, { { 60, 2, .5 }, { 60, 2.5, 1 } }, "dragged onto its tail, the note is cut short");
            move (6); f.expect (clip, { { 60, 2, 1 }, { 60, 6, 1 } }, "and gets its length back when the dragged note moves on");
            f.s.beginEditGesture ("Move MIDI notes"); move (1.5); move (2.5); f.s.endEditGesture();
            f.expect (clip, { { 60, 2, .5 }, { 60, 2.5, 1 } }, "a drag across it in one gesture leaves it as the last step did");
            f.s.undo();
            f.expect (clip, { { 60, 2, 1 }, { 60, 6, 1 } }, "and one undo takes the whole drag back");
        }
        {
            juce::StringArray ground, mine;
            const auto clip = dragCase (ground, mine);
            ok (f.s.updateMidiNotes (clip, { moved (mine[0], 60, 1.5, 1) }), "drag the note over the other");
            f.s.setSelectedNotes (clip, mine);
            ok (f.s.updateMidiNotes (clip, { moved (mine[0], 60, 1.75, 1) }), "and on, the same note still selected");
            ok (f.s.updateMidiNotes (clip, { moved (mine[0], 60, 4, 1) }), "and off it");
            f.expect (clip, { { 60, 2, 1 }, { 60, 4, 1 } }, "selecting the same notes again keeps what they took away restorable");
            ok (f.s.updateMidiNotes (clip, { moved (mine[0], 60, 1.5, 1) }), "drag it back over the other");
            f.s.setSelectedNotes (clip, ground);
            f.s.setSelectedNotes (clip, mine);
            ok (f.s.updateMidiNotes (clip, { moved (mine[0], 60, 4, 1) }), "select something else, then the note again, and drag it off");
            f.expect (clip, { { 60, 4, 1 } }, "once the selection changes, what the note took away stays away");
        }
        {
            juce::StringArray ground, mine;
            const auto clip = dragCase (ground, mine);
            ok (f.s.updateMidiNotes (clip, { moved (mine[0], 60, 1.5, 1) }), "drag the note over the other");
            ok (f.s.removeMidiNotes (clip, mine), "delete the dragged note");
            f.expect (clip, { { 60, 2, 1 } }, "deleting the dragged notes brings back what they took away");
            check (f.holds (clip, ground[0]), "with its identity");
            f.s.undo();
            f.expect (clip, { { 60, 1.5, 1 } }, "and one undo takes the delete back");
        }
        {
            juce::StringArray ground, mine;
            const auto clip = dragCase (ground, mine);
            ok (f.s.updateMidiNotes (clip, { moved (mine[0], 60, 1.5, 1) }), "drag the note over the other");
            f.s.undo();
            f.expect (clip, { { 60, 0, 1 }, { 60, 2, 1 } }, "undo puts both back");
            f.s.redo();
            ok (f.s.updateMidiNotes (clip, { moved (mine[0], 60, 4, 1) }), "redo, then drag the note off");
            f.expect (clip, { { 60, 4, 1 } }, "after undo and redo, what the note took away stays away");
        }
        {
            juce::StringArray ground, mine, other;
            const auto clip = dragCase (ground, mine);
            ok (f.s.updateMidiNotes (clip, { moved (mine[0], 60, 1.5, 1) }), "drag the note over the other");
            ok (f.s.addMidiNotes (clip, { note (64, 8, 1) }, other), "add a note elsewhere");
            ok (f.s.updateMidiNotes (clip, { moved (mine[0], 60, 4, 1) }), "drag the first note off");
            f.expect (clip, { { 60, 4, 1 }, { 64, 8, 1 } }, "any other edit keeps what the note took away");
        }
        {
            juce::StringArray ground, mine;
            const auto clip = dragCase (ground, mine);
            ok (f.s.updateMidiNotes (clip, { moved (mine[0], 60, 1.5, 1) }), "drag the note over the other");
            ok (f.s.setMidiNotesActive (clip, mine, false), "deactivate the dragged note");
            f.expect (clip, { { 60, 1.5, 1 } }, "a deactivated note still wins");
            ok (f.s.updateMidiNotes (clip, { moved (mine[0], 60, 4, 1) }), "drag it off");
            f.expect (clip, { { 60, 2, 1 }, { 60, 4, 1 } }, "and deactivating the dragged notes keeps what they took away restorable");
        }

        // Quantize settles the notes it moves by its own rule, then they win over the others.
        using Tool = Session::NoteTool;
        const auto quantized = [&] (const std::vector<Note>& ground, const std::vector<Note>& chosen, const std::vector<Shape>& expected, const juce::String& text)
        {
            juce::StringArray ids, chosenIds, result;
            const auto clip = f.clip (ground, ids);
            ok (f.s.addMidiNotes (clip, chosen, chosenIds, false), "notes to quantize, leaving the others whole");
            ok (f.s.applyNoteTool (clip, chosenIds, notetools::defaults (Tool::quantize), result, 1), "quantize them to the beat: " + text);
            f.expect (clip, expected, text);
            return std::pair { clip, chosenIds };
        };
        const auto [held, heldIds] = quantized ({}, { note (60, .9, .5), note (60, 1.1, 1) }, { { 60, 1, 1 } }, "of two quantized notes where one holds the other, the longer stays");
        check (f.holds (held, heldIds[1]), "the longer itself");
        const auto [tie, tieIds] = quantized ({}, { note (60, .9, .5), note (60, 1.1, .5) }, { { 60, 1, .5 } }, "the later when they are as long");
        check (f.holds (tie, tieIds[1]), "the later itself");
        quantized ({}, { note (60, .9, 1.2), note (60, 2.1, 1) }, { { 60, 1, 1 }, { 60, 2, 1 } }, "where they only overlap, the earlier ends where the later starts");
        const auto [ground, groundIds] = quantized ({ note (60, 0, 1.5) }, { note (60, 1.1, .5) }, { { 60, 0, 1 }, { 60, 1, .5 } },
                                                    "quantized notes cut the other notes of their pitch");
        ok (f.s.updateMidiNotes (ground, { moved (groundIds[0], 60, 4, .5) }), "move the quantized note away");
        f.expect (ground, { { 60, 0, 1 }, { 60, 4, .5 } }, "for good: the tool's edit ends there");

        // Rhythm clears its key in the range: a note sounding into it ends at its start, one starting inside goes.
        {
            juce::StringArray ids, result;
            const auto clip = f.clip ({ note (36, 1, .25), note (36, 3.5, 1.5), note (36, 5, .25), note (60, 4, 1) }, ids);
            ok (f.s.applyNoteTool (clip, {}, notetools::defaults (Tool::rhythm), result, .25, std::pair { 4.0, 8.0 }), "Rhythm in beats 4 to 8");
            const auto all = f.notes (clip);
            const auto shapeOf = [&] (const juce::String& id) -> std::optional<Shape>
            {
                for (const auto& n : all) if (n.id == id) return Shape { n.pitch, n.startBeat, n.lengthBeats };
                return std::nullopt;
            };
            check (shapeOf (ids[0]) && same ({ *shapeOf (ids[0]) }, { { 36, 1, .25 } }) && shapeOf (ids[1]) && same ({ *shapeOf (ids[1]) }, { { 36, 3.5, .5 } })
                       && ! shapeOf (ids[2]) && shapeOf (ids[3]),
                   "Rhythm leaves its key before the range, cuts a note sounding into it, and clears one starting inside");
            check (! result.isEmpty() && std::all_of (result.begin(), result.end(), [&] (const juce::String& id)
                                                      { const auto s = shapeOf (id); return s && std::get<0> (*s) == 36 && std::get<1> (*s) >= 4 && std::get<1> (*s) < 8; }),
                   "and adds its own notes there");
        }
        std::cout << "Note overlaps passed." << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL note overlaps: " << e.what() << std::endl; return 1; }
}
}
