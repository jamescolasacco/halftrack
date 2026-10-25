#include "Session.h"
#include "AudioToMidi.h"
#include "WarpStretchers.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Convert Harmony, Melody and Drums to New MIDI Track, as the reference's commands (its manual, chapter 13), with
// Halftrack's own analysis: generated audio of known notes, chords and drum hits is converted, and the notes found
// are compared with what was generated; then the Session commands make the new track and clip.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS convert MIDI: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
constexpr double rate = 48000, pi = 3.14159265358979323846;
// A harmonic tone: six harmonics falling 1/h, a 5 ms attack and a slow decay, added into the buffer.
void tone (juce::AudioBuffer<float>& x, double start, double seconds, int pitch, float level)
{
    const auto f0 = 440.0 * std::pow (2.0, (pitch - 69) / 12.0);
    const auto from = (int) (start * rate), count = (int) (seconds * rate);
    for (int i = 0; i < count && from + i < x.getNumSamples(); ++i)
    {
        const auto t = i / rate;
        double v = 0;
        for (int h = 1; h <= 6; ++h) v += std::sin (2 * pi * f0 * h * t) / h;
        const auto envelope = std::min (1.0, t / 0.005) * std::exp (-t / 1.5);
        x.addSample (0, from + i, (float) (level * envelope * v));
    }
}
void hit (juce::AudioBuffer<float>& x, double start, int note)
{
    const auto sound = audiotomidi::drumSound (note, rate);
    const auto from = (int) (start * rate);
    x.addFrom (0, from, sound, 0, 0, std::min (sound.getNumSamples(), x.getNumSamples() - from));
}
std::vector<audiotomidi::Note> convert (const juce::AudioBuffer<float>& x, audiotomidi::Kind kind)
{
    return audiotomidi::convert (x.getReadPointer (0), x.getNumSamples(), rate, kind, warp::findTransients (x, rate));
}
juce::String describe (const std::vector<audiotomidi::Note>& notes)
{
    juce::StringArray parts;
    for (const auto& n : notes) parts.add (juce::String (n.pitch) + "@" + juce::String (n.start, 3));
    return parts.joinIntoString (" ");
}
juce::File write (const juce::File& file, const juce::AudioBuffer<float>& x)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (1).withBitsPerSample (24));
    if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples())) throw std::runtime_error ("could not write a fixture");
    return file;
}
}

int runConvertMidiTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("convert-midi-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");

        // A melody: four tones half a second apart.
        juce::AudioBuffer<float> melody (1, (int) (2.2 * rate)); melody.clear();
        const int tune[] { 60, 64, 67, 72 };
        for (int i = 0; i < 4; ++i) tone (melody, 0.1 + 0.5 * i, 0.4, tune[i], 0.5f);
        const auto sung = convert (melody, audiotomidi::Kind::melody);
        bool followed = sung.size() == 4;
        for (size_t i = 0; followed && i < 4; ++i) followed = sung[i].pitch == tune[i] && std::abs (sung[i].start - (0.1 + 0.5 * (double) i)) < 0.02 && sung[i].length > 0.3;
        check (followed, "a melody of four tones becomes their four notes, each at its onset and lasting about as long (" + describe (sung) + ")");

        // Harmony: a C major chord and then an A minor one.
        juce::AudioBuffer<float> chords (1, (int) (1.8 * rate)); chords.clear();
        for (const auto pitch : { 60, 64, 67 }) tone (chords, 0.1, 0.7, pitch, 0.25f);
        for (const auto pitch : { 57, 60, 64 }) tone (chords, 0.9, 0.7, pitch, 0.25f);
        const auto heard = convert (chords, audiotomidi::Kind::harmony);
        std::vector<int> first, second;
        for (const auto& n : heard) (n.start < 0.5 ? first : second).push_back (n.pitch);
        check (first == std::vector<int> { 60, 64, 67 } && second == std::vector<int> { 57, 60, 64 }, "two chords become their three notes each (" + describe (heard) + ")");

        // Drums: Halftrack's own kick, hi-hat, snare and hi-hat.
        juce::AudioBuffer<float> beat (1, (int) (1.4 * rate)); beat.clear();
        const std::pair<double, int> hits[] { { 0.05, audiotomidi::kickNote }, { 0.35, audiotomidi::hihatNote }, { 0.65, audiotomidi::snareNote }, { 0.95, audiotomidi::hihatNote } };
        for (const auto& [at, note] : hits) hit (beat, at, note);
        const auto played = convert (beat, audiotomidi::Kind::drums);
        bool told = played.size() == 4;
        for (size_t i = 0; told && i < 4; ++i) told = played[i].pitch == hits[i].second && std::abs (played[i].start - hits[i].first) < 0.02;
        check (told, "a kick, hi-hat, snare and hi-hat become C1, F#1, D1 and F#1 at their onsets (" + describe (played) + ")");
        juce::AudioBuffer<float> silence (1, (int) rate); silence.clear();
        check (convert (silence, audiotomidi::Kind::melody).empty(), "silence has no notes");

        // The Session commands: a new MIDI track after the clip's, with the notes in a clip in its place.
        Session s (false, makeSyntheticTestDeviceType());
        ok (s.saveProject (root.getChildFile ("Convert").getChildFile ("Convert.halftrack")), "save the project");
        ok (s.importAudio (write (root.getChildFile ("melody.wav"), melody), 0, 2.0), "put the melody on the first track, at 2 s");
        const auto source = s.snapshot().tracks[0].clips.front();
        juce::String trackId;
        ok (s.convertToNewMidiTrack (source.id, Session::ConvertKind::melody, trackId), "Convert Melody to New MIDI Track");
        auto snap = s.snapshot();
        const auto beatsPerSecond = snap.tempo / 60;
        check (snap.tracks.size() == 3 && snap.tracks[1].id == trackId && snap.tracks[1].isMidi && snap.tracks[1].clips.size() == 1
                   && std::abs (snap.tracks[1].clips[0].startBeat - 2.0 * beatsPerSecond) < 1e-6 && std::abs (snap.tracks[1].clips[0].lengthBeats - source.length * beatsPerSecond) < 1e-6,
               "a MIDI track comes after the clip's, its clip in the audio clip's place and of its length");
        const auto& notes = snap.tracks[1].clips[0].notes;
        bool placed = notes.size() == 4;
        for (size_t i = 0; placed && i < 4; ++i) placed = notes[i].pitch == tune[i] && std::abs (notes[i].startBeat - (0.1 + 0.5 * (double) i) * beatsPerSecond) < 0.04;
        check (placed && s.undoSteps()[0] == "Convert Melody to New MIDI Track", "its clip holds the melody's notes at their times, one undo step");
        s.undo();
        check (s.snapshot().tracks.size() == 2, "undo removes the new track");

        ok (s.importAudio (write (root.getChildFile ("beat.wav"), beat), 1, 0), "put the drums on the second track");
        const auto drumClip = s.snapshot().tracks[1].clips.front();
        ok (s.convertToNewMidiTrack (drumClip.id, Session::ConvertKind::drums, trackId), "Convert Drums to New MIDI Track");
        snap = s.snapshot();
        const auto& kit = snap.tracks[2].instrument;
        std::vector<int> pads;
        bool sounds = true;
        for (const auto& chain : kit.chains)
        {
            pads.push_back (chain.settings.receiveNote);
            sounds = sounds && ! chain.devices.empty() && chain.devices.front().sample.existsAsFile() && chain.devices.front().sample.isAChildOf (s.projectAssets());
        }
        std::sort (pads.begin(), pads.end());
        check (snap.tracks[2].id == trackId && kit.isRack && pads == std::vector<int> { 36, 38, 42 } && sounds,
               "its track plays a Drum Rack of three pads, C1, D1 and F#1, with Halftrack's own sounds kept in the project's media");
        std::vector<int> pitches;
        for (const auto& n : snap.tracks[2].clips[0].notes) pitches.push_back (n.pitch);
        check (pitches == std::vector<int> { 36, 42, 38, 42 }, "and its clip the four hits");
        s.undo();

        check (s.convertToNewMidiTrack ("missing", Session::ConvertKind::harmony, trackId).failed(), "an unknown clip is refused");
        ok (s.importAudio (write (root.getChildFile ("silence.wav"), silence), 0, 8), "put a second of silence on the first track");
        const auto silent = s.snapshot().tracks[0].clips.back();
        const auto tracksBefore = s.snapshot().tracks.size();
        const auto refused = s.convertToNewMidiTrack (silent.id, Session::ConvertKind::harmony, trackId);
        check (refused.failed() && refused.getErrorMessage().contains ("No notes") && s.snapshot().tracks.size() == tracksBefore, "audio without notes is refused, adding no track");
        std::cout << "Convert MIDI passed: " << root.getFullPathName() << std::endl;
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cout << "FAIL convert MIDI: " << e.what() << std::endl;
        return 1;
    }
}
}
