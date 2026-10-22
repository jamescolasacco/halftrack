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
    ok (s.exportStems (folder, settings, written, true), "render the stems");
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
        root.deleteRecursively();
        std::cout << "Range clipboard passed" << std::endl;
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL range clipboard: " << e.what() << std::endl; return 1; }
}
}
