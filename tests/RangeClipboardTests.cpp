#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Copy, cut and paste of a time selection. An audio track of alternating loud and quiet clicks
// on the beat sits above a MIDI track of the same pattern in notes, so each beat of a render tells
// what played there. A two-beat range is copied from both tracks and pasted later, then over
// existing material, then into another project, and each track is rendered on its own.
namespace halftrack
{
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS range clipboard: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
constexpr int beatSamples = 24000; // A beat at 120 BPM and 48 kHz.

// Loud, quiet or silent beats as L, q and -, from one track's stem. Rendering stems is no edit, so
// undo and redo are left as they were.
juce::String pattern (Session& s, int track, const juce::File& folder, int beats)
{
    folder.deleteRecursively();
    juce::StringArray written;
    Session::AudioExportSettings settings;
    settings.stemTracks.add (s.snapshot().tracks[(size_t) track].id); // Only the track read.
    ok (s.exportStems (folder, settings, written, true), "render the track's stem");
    const auto snapshot = s.snapshot();
    const auto file = folder.getChildFile (juce::File::createLegalFileName (snapshot.name + " " + snapshot.tracks[(size_t) track].name) + ".wav");
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(), true));
    if (reader == nullptr) throw std::runtime_error ("could not read the render");
    juce::AudioBuffer<float> audio (2, (int) reader->lengthInSamples);
    reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);
    std::vector<float> levels;
    float loudest = 0;
    for (int b = 0; b < beats; ++b)
    {
        levels.push_back (b * beatSamples + beatSamples / 8 <= audio.getNumSamples() ? audio.getMagnitude (b * beatSamples, beatSamples / 8) : 0.0f);
        loudest = std::max (loudest, levels.back());
    }
    juce::String result;
    for (auto level : levels) result << (level > loudest * .7f ? "L" : level > loudest * .1f ? "q" : "-");
    return result;
}
}

int runRangeClipboardTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("range-clipboard-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto render = root.getChildFile ("stems");
        const auto clicks = root.getChildFile ("clicks.wav");
        {
            // Four beats: loud, quiet, loud, quiet.
            juce::AudioBuffer<float> audio (1, 4 * beatSamples);
            audio.clear();
            for (int b = 0; b < 4; ++b)
                for (int n = 0; n < 480; ++n) audio.setSample (0, b * beatSamples + n, (b % 2 == 0 ? .8f : .2f) * std::exp (-n / 80.0f) * (n % 2 == 0 ? 1.0f : -1.0f));
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream (clicks.createOutputStream());
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (1).withBitsPerSample (24));
            check (writer != nullptr && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()), "write four beats of clicks");
        }
        Session s (false);
        ok (s.saveProject (root.getChildFile ("First").getChildFile ("First.halftrack")), "save a project");
        ok (s.addAudioTrack ("Clicks"), "add an audio track");
        const int audio = (int) s.snapshot().tracks.size() - 1;
        ok (s.importAudio (clicks, audio, 0), "import the clicks");
        ok (s.addMidiTrack ("Keys"), "add a MIDI track below it");
        const int midi = (int) s.snapshot().tracks.size() - 1;
        auto keys = s.snapshot().tracks[(size_t) midi];
        auto synth = keys.synth; synth.attackSeconds = synth.decaySeconds = 0; synth.sustain = 1; synth.releaseSeconds = .01f;
        ok (s.setSynthSettings (keys.id, synth), "a synth with instant attack");
        ok (s.addMidiClip (keys.id, 0, 4), "a one-bar MIDI clip");
        juce::StringArray added;
        ok (s.addMidiNotes (s.snapshot().tracks[(size_t) midi].clips[0].id, { { "", 60, 0, .25, 1.0f }, { "", 60, 1, .25, .25f }, { "", 60, 2, .25, 1.0f }, { "", 60, 3, .25, .25f } }, added),
            "notes loud, quiet, loud, quiet");
        const auto audioId = s.snapshot().tracks[(size_t) audio].id, midiId = s.snapshot().tracks[(size_t) midi].id;
        check (pattern (s, audio, render, 10) == "LqLq------" && pattern (s, midi, render, 10) == "LqLq------", "both tracks play loud, quiet, loud, quiet");

        // Copy beats 2 and 3 of both tracks.
        const bool couldUndo = s.canUndo();
        const auto before = s.snapshot();
        ok (s.copyRange ({ audioId, midiId }, .5, 1.5), "copy beats 2 and 3 of both tracks");
        check (s.hasRangeClipboard() && std::abs (s.rangeClipboardLength() - 1) < 1e-9 && s.canUndo() == couldUndo
                   && s.snapshot().tracks[(size_t) audio].clips.size() == before.tracks[(size_t) audio].clips.size() && s.snapshot().tracks[(size_t) midi].clips.size() == before.tracks[(size_t) midi].clips.size(),
               "copying keeps the selection and changes nothing");
        ok (s.pasteRange (audioId, 3), "paste them at beat 7 from the audio track");
        check (pattern (s, audio, render, 10) == "LqLq--qL--" && pattern (s, midi, render, 10) == "LqLq--qL--", "each track plays the copied beats there");
        const auto pastedAudio = s.snapshot().tracks[(size_t) audio].clips.back();
        check (std::abs (pastedAudio.start - 3) < 1e-6 && std::abs (pastedAudio.length - 1) < 1e-6 && std::abs (pastedAudio.sourceOffset - .5) < 1e-6
                   && pastedAudio.file.isAChildOf (root.getChildFile ("First")),
               "the pasted audio clip holds just the range, its audio collected into the project");
        s.undo();
        check (pattern (s, audio, render, 10) == "LqLq------" && s.snapshot().tracks[(size_t) midi].clips.size() == 1, "one undo removes the paste from both tracks");
        s.redo();
        check (pattern (s, midi, render, 10) == "LqLq--qL--", "redo puts it back");

        // Pasted over existing material, it replaces just its range.
        ok (s.pasteRange (audioId, 1), "paste over the third and fourth beats");
        check (pattern (s, audio, render, 10) == "LqqL--qL--" && pattern (s, midi, render, 10) == "LqqL--qL--", "the range is replaced and the beats around it stay");
        s.undo();

        // Refusals.
        check (s.pasteRange (midiId, 0).failed(), "pasting onto too few tracks is refused");
        ok (s.addAudioTrack ("More"), "add another audio track below");
        check (s.pasteRange (midiId, 0).failed(), "pasting audio onto a MIDI track is refused");
        check (s.copyRange ({ audioId }, .5, 5).wasOk() && s.copyRange ({ "missing" }, 0, 1).failed() && s.copyRange ({ audioId }, 1, 1).failed(),
               "a range past the clips copies; a missing track or an empty range is refused");
        s.undo();

        // Cut.
        ok (s.copyRange ({ audioId, midiId }, .5, 1.5, true), "cut beats 2 and 3 of both tracks");
        {
            const auto cutAudio = pattern (s, audio, render, 10), cutMidi = pattern (s, midi, render, 10);
            check (cutAudio == "L--q--qL--" && cutMidi == "L--q--qL--" && s.hasRangeClipboard(), "cutting removes just them and keeps them (" + cutAudio + ", " + cutMidi + ")");
        }
        ok (s.pasteRange (audioId, .5), "paste them back");
        check (pattern (s, audio, render, 10) == "LqLq--qL--" && pattern (s, midi, render, 10) == "LqLq--qL--", "pasted back, the tracks play as before");

        // Another project: the audio comes with it.
        ok (s.copyRange ({ audioId, midiId }, 0, 1), "copy the first two beats");
        ok (s.newProject(), "start a new project");
        check (s.hasRangeClipboard(), "the clipboard outlives the project");
        ok (s.saveProject (root.getChildFile ("Second").getChildFile ("Second.halftrack")), "save the new project");
        ok (s.addAudioTrack ("Audio"), "an audio track");
        const int newAudio = (int) s.snapshot().tracks.size() - 1;
        ok (s.addMidiTrack ("Keys"), "and a MIDI track");
        const int newMidi = (int) s.snapshot().tracks.size() - 1;
        synth = s.snapshot().tracks[(size_t) newMidi].synth; synth.attackSeconds = synth.decaySeconds = 0; synth.sustain = 1; synth.releaseSeconds = .01f;
        ok (s.setSynthSettings (s.snapshot().tracks[(size_t) newMidi].id, synth), "the same synth");
        ok (s.pasteRange (s.snapshot().tracks[(size_t) newAudio].id, 0), "paste into it");
        const auto collected = s.snapshot().tracks[(size_t) newAudio].clips.front().file;
        check (collected.existsAsFile() && collected.isAChildOf (root.getChildFile ("Second")) && pattern (s, newAudio, render, 4) == "Lq--" && pattern (s, newMidi, render, 4) == "Lq--",
               "it plays in the new project, from audio collected into it");

        // Clip copies and range copies replace each other.
        ok (s.copyMidiClip (s.snapshot().tracks[(size_t) newMidi].clips[0].id), "copy a MIDI clip");
        check (! s.hasRangeClipboard() && s.hasMidiClipClipboard(), "a clip copy clears the range");
        ok (s.copyRange ({ s.snapshot().tracks[(size_t) newAudio].id }, 0, 1), "copy a range again");
        check (s.hasRangeClipboard() && ! s.hasMidiClipClipboard() && ! s.hasAudioClipClipboard(), "a range copy clears the clip");

        // Automation comes with the range: a volume dip at beat 1.5 copied with beats 2 and 3.
        const auto audioTrack = s.snapshot().tracks[(size_t) newAudio].id;
        ok (s.setTrackAutomation (audioTrack, Session::AutomationTarget::volume, { true, { { 0, 0 }, { 1.5, -24 }, { 2.5, 0 } } }), "automate a volume dip");
        ok (s.copyRange ({ audioTrack }, .5, 1.5), "copy beats 2 and 3 with it");
        ok (s.pasteRange (audioTrack, 3), "paste them at beat 7");
        const auto points = s.snapshot().tracks[(size_t) newAudio].volumeAutomation.points;
        const auto has = [&] (double beat, float decibels)
        { return std::any_of (points.begin(), points.end(), [&] (const auto& p) { return std::abs (p.beat - beat) < 1e-3 && std::abs (p.value - decibels) < .5f; }); };
        check (has (0, 0) && has (1.5, -24) && has (2.5, 0) && has (6.5, -24) && has (8, 0),
               "the dip is pasted five beats on and the volume returns after it, the original kept");
        s.undo();
        check (s.snapshot().tracks[(size_t) newAudio].volumeAutomation.points.size() == 3, "undo removes the pasted automation with the clips");

        // Selected clips: their copy holds just them, cutting removes just them, and a clip copies to a
        // time on a track of its kind, keeping its beats.
        ok (s.newProject(), "a third project");
        ok (s.saveProject (root.getChildFile ("Third").getChildFile ("Third.halftrack")), "saved");
        ok (s.addMidiTrack ("Lead"), "a MIDI track");
        int lead = -1;
        for (int t = 0; t < (int) s.snapshot().tracks.size(); ++t) if (s.snapshot().tracks[(size_t) t].name == "Lead") lead = t;
        check (lead >= 0, "it is in the project");
        const auto leadId = s.snapshot().tracks[(size_t) lead].id;
        synth = s.snapshot().tracks[(size_t) lead].synth; synth.attackSeconds = synth.decaySeconds = 0; synth.sustain = 1; synth.releaseSeconds = .01f;
        ok (s.setSynthSettings (leadId, synth), "a synth with instant attack");
        juce::StringArray ids;
        for (const auto& [beat, velocity] : { std::pair { 0.0, 1.0f }, std::pair { 1.0, .25f }, std::pair { 2.0, 1.0f } })
        {
            ok (s.addMidiClip (leadId, beat, 1), "a one-beat clip");
            const auto clips = s.snapshot().tracks[(size_t) lead].clips;
            for (const auto& clip : clips)
                if (std::abs (clip.startBeat - beat) < 1e-9) { ids.add (clip.id); ok (s.addMidiNotes (clip.id, { { "", 60, 0, .25, velocity } }, added), "its note"); }
        }
        check (ids.size() == 3 && pattern (s, lead, render, 6) == "LqL---", "three clips play loud, quiet, loud");
        ok (s.copyRange ({ leadId }, 0, 1.5, false, { ids[0], ids[2] }), "copy the first and third clips alone");
        ok (s.pasteRange (leadId, 2), "paste them at beat 5");
        check (pattern (s, lead, render, 8) == "LqL-L-L-", "the paste holds just those clips, in their places");
        s.undo();
        ok (s.copyRange ({ leadId }, 0, 1.5, true, { ids[0], ids[2] }), "cut the first and third clips");
        check (pattern (s, lead, render, 6) == "-L----" && s.snapshot().tracks[(size_t) lead].clips.size() == 1, "cutting removes just them, the second (now the loudest) playing alone");
        s.undo();
        check (pattern (s, lead, render, 6) == "LqL---", "and one undo puts them back");
        juce::String copy;
        ok (s.copyClipTo (ids[1], leadId, 2, copy), "copy the second clip to beat 5");
        check (copy.isNotEmpty() && copy != ids[1] && pattern (s, lead, render, 8) == "LqL-q---", "a copy of its own plays there, the original kept");
        s.undo();
        check (s.snapshot().tracks[(size_t) lead].clips.size() == 3, "one undo removes the copy");
        ok (s.addAudioTrack ("Audio"), "an audio track");
        check (s.copyClipTo (ids[1], s.snapshot().tracks.back().id, 2, copy).failed() && s.copyClipTo ("missing", leadId, 2, copy).failed(),
               "a MIDI clip is not copied to an audio track, nor a missing clip");
        ok (s.setTempoMap ({ { 0, 120 }, { 4, 60 } }), "slow to 60 BPM at beat 5");
        ok (s.copyClipTo (ids[1], leadId, s.secondsAt (6), copy), "copy the second clip to beat 7, after the change");
        const auto clips = s.snapshot().tracks[(size_t) lead].clips;
        for (const auto& clip : clips)
            if (clip.id == copy) check (std::abs (clip.startBeat - 6) < 1e-6 && std::abs (clip.lengthBeats - 1) < 1e-6, "the copy keeps its one beat at the slower tempo");
        ok (s.moveMidiClip (ids[1], leadId, s.secondsAt (8)), "move the second clip to beat 9");
        const auto moved = s.snapshot().tracks[(size_t) lead].clips;
        for (const auto& clip : moved)
            if (clip.id == ids[1]) check (std::abs (clip.startBeat - 8) < 1e-6 && std::abs (clip.lengthBeats - 1) < 1e-6, "a moved clip keeps its one beat too (" + juce::String (clip.lengthBeats, 3) + ")");
        root.deleteRecursively();
        std::cout << "Range clipboard passed" << std::endl;
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL range clipboard: " << e.what() << std::endl; return 1; }
}
}
