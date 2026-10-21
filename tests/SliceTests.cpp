#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Slice to New MIDI Track, rendered offline from decaying 1 kHz hits on every beat at 120 BPM.
// Slicing at transients, or every quarter or eighth note, makes a Drum Rack track whose MIDI clip
// plays the slices back to the original audio; a warped clip's slices keep their own speed at a
// new tempo, landing on the new beats.
namespace halftrack
{
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS slicing: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
constexpr double rate = 48000;
juce::AudioBuffer<float> render (Session& s, const juce::File& file)
{
    ok (s.exportWav (file, rate, 32, 0), "render");
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    check (reader != nullptr, "read the render");
    juce::AudioBuffer<float> x (2, (int) reader->lengthInSamples); reader->read (&x, 0, x.getNumSamples(), 0, true, true);
    return x;
}
double rms (const juce::AudioBuffer<float>& x, int from, int to)
{
    double sum = 0; to = std::min (to, x.getNumSamples());
    for (int n = from; n < to; ++n) sum += x.getSample (0, n) * x.getSample (0, n);
    return to > from ? std::sqrt (sum / (to - from)) : 0;
}
// The level of the difference between two renders over their shared length, against the first's level.
double difference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int length)
{
    double sum = 0, level = 0;
    for (int n = 0; n < length; ++n) { const auto d = a.getSample (0, n) - b.getSample (0, n); sum += d * d; level += a.getSample (0, n) * a.getSample (0, n); }
    return level > 0 ? std::sqrt (sum / level) : 1;
}
}

int runSliceTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("slicing-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto hits = root.getChildFile ("hits.wav");
        {
            juce::AudioBuffer<float> x (2, (int) (4 * rate));
            for (int c = 0; c < 2; ++c)
                for (int n = 0; n < x.getNumSamples(); ++n)
                {
                    const auto t = std::fmod (n / rate, .5);
                    x.setSample (c, n, (float) (.8 * std::exp (-t / .1) * std::sin (juce::MathConstants<double>::twoPi * 1000 * t)));
                }
            juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (hits.createOutputStream());
            auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (32));
            check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write eight hits on the beats at 120 BPM");
        }
        const auto out = root.getChildFile ("out.wav");
        Session s (false);
        ok (s.importAudio (hits, 0, 0), "import them");
        const auto source = s.snapshot().tracks[0].clips[0];
        const auto original = render (s, out);
        const auto tracksBefore = s.snapshot().tracks.size();

        // At transients: eight slices from C1, a note on each beat.
        juce::String trackId;
        ok (s.sliceToNewMidiTrack (source.id, Session::SliceBy::transients, trackId), "slice at transients");
        auto state = s.snapshot();
        check (state.tracks.size() == tracksBefore + 1 && state.tracks[1].id == trackId && state.tracks[1].isMidi, "a new MIDI track follows the clip's");
        const auto& sliced = state.tracks[1];
        check (sliced.instrument.isRack && sliced.instrument.effect.kind == Session::EffectKind::drumRack && sliced.instrument.chains.size() == 8,
               "its Drum Rack has a pad for each of the eight hits");
        bool notes = sliced.clips.size() == 1 && sliced.clips[0].notes.size() == 8 && std::abs (sliced.clips[0].startBeat) < 1e-9 && std::abs (sliced.clips[0].lengthBeats - 8) < 1e-6;
        juce::String placed;
        for (int i = 0; notes && i < 8; ++i)
        {
            const auto& note = sliced.clips[0].notes[(size_t) i];
            placed << juce::String (note.startBeat, 3) << " ";
            notes = note.pitch == 36 + i && std::abs (note.startBeat - i) < .002 && sliced.instrument.chains[(size_t) i].settings.receiveNote == 36 + i;
        }
        check (notes, "its MIDI clip plays pads 36 to 43 on the beats (" + placed.trim() + ")");
        ok (s.setTrackMute (0, true), "mute the original");
        auto x = render (s, out);
        const auto apart = difference (original, x, (int) (4 * rate));
        check (apart < .001, "the slices play back the original audio (difference " + juce::String (apart * 100, 2) + "% of its level)");
        s.undo();
        s.undo();
        check (s.snapshot().tracks.size() == tracksBefore && ! s.snapshot().tracks[0].mute, "one undo removes the sliced track");

        // At divisions: every quarter note makes the same eight; every eighth, sixteen.
        ok (s.sliceToNewMidiTrack (source.id, Session::SliceBy::quarter, trackId), "slice every quarter note");
        check (s.snapshot().tracks[1].clips[0].notes.size() == 8, "eight slices");
        s.undo();
        ok (s.sliceToNewMidiTrack (source.id, Session::SliceBy::eighth, trackId), "slice every eighth note");
        state = s.snapshot();
        check (state.tracks[1].clips[0].notes.size() == 16 && std::abs (state.tracks[1].clips[0].notes[3].startBeat - 1.5) < 1e-9, "sixteen, half a beat apart");
        ok (s.setTrackMute (0, true), "mute the original");
        check (difference (original, render (s, out), (int) (4 * rate)) < .02, "they also play back the original");
        s.undo(); s.undo();

        // Refusals change nothing.
        check (s.sliceToNewMidiTrack (source.id, Session::SliceBy::warpMarkers, trackId).failed(), "an unwarped clip has no markers to slice at");
        check (s.sliceToNewMidiTrack ("nothing", Session::SliceBy::quarter, trackId).failed() && s.snapshot().tracks.size() == tracksBefore, "an unknown clip is refused");

        // A warped clip at a slower tempo: the slices keep their own speed and land on the new beats.
        ok (s.setAudioClipWarp (source.id, { Session::WarpMode::stretch, 120 }), "warp the clip at 120 BPM");
        s.setTempo (60);
        ok (s.sliceToNewMidiTrack (source.id, Session::SliceBy::transients, trackId), "slice the warped clip at transients");
        state = s.snapshot();
        check (state.tracks[1].clips[0].notes.size() == 8 && std::abs (state.tracks[1].clips[0].notes[5].startBeat - 5) < .002, "a note on each of its beats");
        ok (s.setTrackMute (0, true), "mute the original");
        x = render (s, out);
        bool landed = true; juce::String levels;
        for (int k = 0; k < 8; ++k)
        {
            const auto at = (int) (k * rate);
            levels << juce::String (rms (x, at, at + (int) (.05 * rate)), 3) << " ";
            landed = landed && std::abs (rms (x, at, at + (int) (.05 * rate)) - rms (original, 0, (int) (.05 * rate))) < .01 && rms (x, at + (int) (.6 * rate), at + (int) (.95 * rate)) < 1e-3;
        }
        check (landed, "each hit plays at its own speed on the 60 BPM beats, then silence (" + levels.trim() + ")");
        s.undo(); s.undo();
        std::cout << "Slicing passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL slicing: " << e.what() << std::endl; return 1; }
}
}
