#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// MIDI controller lanes at 120 BPM. The built-in synthesizer plays a sine C3; a pitch
// bend lane must move it by exactly two semitones at its point, and a sustain lane must
// hold a short note until the pedal is released. Pitch is measured from interpolated
// zero crossings of the render.
namespace halftrack
{
namespace
{
constexpr double rate = 48000;
using Point = Session::ControllerPoint;
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS controllers: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
juce::AudioBuffer<float> render (Session& s, const juce::File& file)
{
    ok (s.exportWav (file, rate, 32, 1), "render");
    juce::AudioFormatManager formats; formats.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (file));
    check (r != nullptr, "read render"); juce::AudioBuffer<float> x (2, (int) r->lengthInSamples); r->read (&x, 0, x.getNumSamples(), 0, true, true); return x;
}
// Frequency from the first and last rising zero crossings inside [from, to) seconds.
double frequency (const juce::AudioBuffer<float>& x, double from, double to)
{
    double first = -1, last = -1; int cycles = -1;
    for (int n = (int) (from * rate); n + 1 < (int) (to * rate) && n + 1 < x.getNumSamples(); ++n)
    {
        const auto a = x.getSample (0, n), b = x.getSample (0, n + 1);
        if (a < 0 && b >= 0)
        {
            const double at = n + a / (a - b);
            if (first < 0) first = at;
            last = at; ++cycles;
        }
    }
    return cycles > 0 ? cycles * rate / (last - first) : 0;
}
double rms (const juce::AudioBuffer<float>& x, double from, double to)
{
    const int start = (int) (from * rate), end = std::min (x.getNumSamples(), (int) (to * rate));
    return end > start ? x.getRMSLevel (0, start, end - start) : 0;
}
std::vector<Session::ControllerLane> lanes (Session& s)
{ const auto snapshot = s.snapshot(); return snapshot.tracks.back().clips.empty() ? std::vector<Session::ControllerLane> {} : snapshot.tracks.back().clips[0].controllers; }
}

int runMidiControllerTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("controllers-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto file = root.getChildFile ("render.wav");
        Session s (false);
        ok (s.addMidiTrack ("Bend"), "add a MIDI track");
        const auto trackId = s.snapshot().tracks.back().id;
        Session::SynthSettings synth; synth.waveform = 2; synth.attackSeconds = .001f; synth.decaySeconds = .01f; synth.sustain = 1; synth.releaseSeconds = .05f; synth.cutoffHz = 18000;
        ok (s.setSynthSettings (trackId, synth), "a sine synthesizer");
        ok (s.addMidiClip (trackId, 0, 4), "a four-beat clip");
        const auto clip = s.snapshot().tracks.back().clips[0].id;
        ok (s.addMidiNote (clip, 60, 0, 4, .8f), "hold C3 for four beats");
        const double c3 = 440 * std::pow (2.0, (60 - 69) / 12.0), d3 = c3 * std::pow (2.0, 2 / 12.0), aSharp2 = c3 * std::pow (2.0, -2 / 12.0);
        auto x = render (s, file);
        check (std::abs (frequency (x, .1, .95) - c3) < .05, "without a lane the note plays at " + juce::String (c3, 3) + " Hz");

        check (s.setMidiControllerLane (clip, 120, { { 0, 0 } }).failed() && s.setMidiControllerLane (clip, 1, { { 0, 128 } }).failed()
               && s.setMidiControllerLane (clip, Session::pitchBendLane, { { 0, 8192 } }).failed() && s.setMidiControllerLane (clip, 1, { { 1, 0 }, { 1, 5 } }).failed()
               && s.setMidiControllerLane (clip, 1, { { 4.5, 0 } }).failed() && s.setMidiControllerLane ("missing", 1, {}).failed() && lanes (s).empty(),
               "channel mode messages, out-of-range values, repeated beats, points past the clip and unknown clips are rejected");
        ok (s.setMidiControllerLane (clip, Session::pitchBendLane, { { 0, 0 }, { 2, 8191 } }), "bend up two semitones at beat 2");
        check (lanes (s).size() == 1 && lanes (s)[0].controller == Session::pitchBendLane && lanes (s)[0].points == std::vector<Point> { { 0, 0 }, { 2, 8191 } },
               "the clip reports the pitch bend lane");
        x = render (s, file);
        check (std::abs (frequency (x, .1, .95) - c3) < .05 && std::abs (frequency (x, 1.05, 1.95) - d3) < .05, "the render bends to " + juce::String (d3, 3) + " Hz at beat 2");
        ok (s.setMidiControllerLane (clip, Session::pitchBendLane, { { 0, -8192 } }), "bend down two semitones from the start");
        x = render (s, file);
        check (std::abs (frequency (x, .1, 1.95) - aSharp2) < .05, "a full downward bend plays " + juce::String (aSharp2, 3) + " Hz");
        s.undo(); check (lanes (s)[0].points.size() == 2, "undo restores the upward bend");
        s.redo(); check (lanes (s)[0].points == std::vector<Point> { { 0, -8192 } }, "redo restores the downward bend");
        ok (s.setMidiControllerLane (clip, Session::pitchBendLane, {}), "clear the bend");
        check (lanes (s).empty(), "an empty lane is removed");

        // Sustain: a quarter-beat note rings until the pedal lifts at beat 3.
        ok (s.updateMidiNote (clip, s.snapshot().tracks.back().clips[0].notes[0].id, 60, 0, .25, .8f), "shorten the note to a sixteenth");
        x = render (s, file);
        check (rms (x, .3, 1.4) < 1e-4, "without the pedal the short note has stopped by 0.3 s");
        ok (s.setMidiControllerLane (clip, 64, { { 0, 127 }, { 3, 0 } }), "hold the sustain pedal until beat 3");
        x = render (s, file);
        check (rms (x, .3, 1.4) > .05 && rms (x, 1.65, 1.95) < 1e-4, "the pedal holds the note until beat 3 and releasing it ends the note");
        ok (s.setMidiControllerLane (clip, 1, { { 0, 0 }, { 1, 64 }, { 2, 127 } }), "add a mod wheel lane");
        check (lanes (s).size() == 2 && lanes (s)[0].controller == 1 && lanes (s)[1].controller == 64, "lanes are listed by controller number");

        // Time edits carry controller points with the notes.
        ok (s.insertTime (.25, .5), "insert one beat at half a beat");
        auto edited = lanes (s);
        check (edited[0].points == std::vector<Point> { { 0, 0 }, { 2, 64 }, { 3, 127 } } && edited[1].points == std::vector<Point> { { 0, 127 }, { 4, 0 } },
               "inserting time moves later controller points");
        s.undo();
        ok (s.deleteTime (.25, .75), "delete from half a beat to two beats");
        edited = lanes (s);
        check (edited[0].points == std::vector<Point> { { 0, 0 }, { .5, 127 } } && edited[1].points == std::vector<Point> { { 0, 127 }, { 1.5, 0 } },
               "deleting time keeps the last value set inside it at the cut and moves later points");
        s.undo();
        check (lanes (s)[0].points.size() == 3, "undo restores the lanes");

        // Reopening, MIDI files and duplicates.
        const auto before = lanes (s);
        const auto project = root.getChildFile ("Lanes").getChildFile ("Lanes.halftrack");
        ok (project.getParentDirectory().createDirectory(), "create the project folder");
        ok (s.saveProject (project), "save"); ok (s.loadProject (project), "reopen");
        check (lanes (s) == before, "controller lanes reopen unchanged");
        const auto midiFile = root.getChildFile ("lanes.mid");
        ok (s.exportMidiClip (s.snapshot().tracks.back().clips[0].id, midiFile), "export the clip as a MIDI file");
        std::vector<juce::String> imported;
        ok (s.importMidiFile (midiFile, 8, imported), "import it again");
        std::vector<Session::ControllerLane> reimported;
        { const auto snapshot = s.snapshot(); for (const auto& t : snapshot.tracks) for (const auto& c : t.clips) if (! imported.empty() && c.id == imported[0]) reimported = c.controllers; }
        check (reimported == before, "a MIDI file round trip keeps the lanes");
        juce::String copy;
        ok (s.duplicateMidiClip (s.snapshot().tracks.back().clips[0].id, copy), "duplicate the clip");
        bool same = false;
        { const auto snapshot = s.snapshot(); for (const auto& t : snapshot.tracks) for (const auto& c : t.clips) if (c.id == copy) same = c.controllers == before; }
        check (same, "a duplicated clip keeps its lanes");
        std::cout << "Controllers passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL controllers: " << e.what() << std::endl; return 1; }
}
}
