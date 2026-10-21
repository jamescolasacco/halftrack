#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Re-Pitch warping, rendered offline with a 1 kHz sine recorded at 120 BPM. A Re-Pitch clip keeps
// its length in beats by playing faster or slower, so its pitch follows the tempo: at 132 BPM it
// sounds 1.1 kHz, and under a tempo map it changes where the tempo does. A stretched clip keeps
// its 1 kHz pitch at the same tempo. The mode is one undo step and reopens.
namespace halftrack
{
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS repitch: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
juce::AudioBuffer<float> render (Session& s, const juce::File& file)
{
    ok (s.exportWav (file, 48000, 32, 0), "render " + file.getFileName());
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    check (reader != nullptr, "read " + file.getFileName());
    juce::AudioBuffer<float> x (2, (int) reader->lengthInSamples); reader->read (&x, 0, x.getNumSamples(), 0, true, true);
    return x;
}
// The frequency over a window, from its rising zero crossings.
double frequency (const juce::AudioBuffer<float>& x, double fromSeconds, double seconds)
{
    const int from = (int) (fromSeconds * 48000), to = std::min (x.getNumSamples(), from + (int) (seconds * 48000));
    int first = -1, last = -1, crossings = 0;
    for (int n = from + 1; n < to; ++n)
        if (x.getSample (0, n - 1) < 0 && x.getSample (0, n) >= 0) { if (first < 0) first = n; last = n; ++crossings; }
    return crossings > 1 ? (crossings - 1) * 48000.0 / (last - first) : 0;
}
bool near (double a, double b, double relative) { return std::abs (a - b) <= b * relative; }
}

int runRepitchTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("repitch-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto source = root.getChildFile ("sine.wav");
        {
            juce::AudioBuffer<float> x (2, 8 * 48000);
            for (int c = 0; c < 2; ++c) for (int n = 0; n < x.getNumSamples(); ++n) x.setSample (c, n, (float) (.5 * std::sin (juce::MathConstants<double>::twoPi * 1000.0 * n / 48000)));
            juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (source.createOutputStream());
            auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (32));
            check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write an 8 s 1 kHz sine");
        }
        Session s (false);
        ok (s.importAudio (source, 0, 0), "import it");
        const auto clip = [&] { return s.snapshot().tracks[0].clips[0]; };
        const auto id = clip().id;
        const auto out = root.getChildFile ("out.wav");
        ok (s.setAudioClipWarp (id, { Session::WarpMode::repitch, 120 }), "warp it Re-Pitch at 120 BPM");
        check (clip().warp.mode == Session::WarpMode::repitch && std::abs (clip().length - 8) < 1e-3, "at its own tempo the clip keeps its 8 seconds");
        check (near (frequency (render (s, out), 1, 2), 1000, .005), "and its 1 kHz pitch");

        s.setTempo (132);
        auto x = render (s, out);
        check (std::abs (clip().length - 8 * 120.0 / 132) < 1e-3, "at 132 BPM it plays for 120/132 of the time");
        const auto faster = frequency (x, 1, 2);
        check (near (faster, 1100, .005), "and sounds 1.1 times higher (" + juce::String (faster, 1) + " Hz)");
        ok (s.setAudioClipWarp (id, { Session::WarpMode::stretch, 120 }), "stretch it instead");
        const auto stretched = frequency (render (s, out), 1, 2);
        check (near (stretched, 1000, .01), "a stretched clip keeps its pitch at that tempo (" + juce::String (stretched, 1) + " Hz)");
        s.undo();
        check (clip().warp.mode == Session::WarpMode::repitch, "one undo returns to Re-Pitch");

        // A tempo map: 120 BPM to beat 8 (four seconds in), then a quarter-beat ramp to 240. Tempo
        // points ramp linearly between them.
        ok (s.setTempoMap ({ { 0, 120 }, { 8, 120 }, { 8.25, 240 } }), "a tempo map doubling the tempo after beat 8");
        x = render (s, out);
        const auto before = frequency (x, 1, 2), after = frequency (x, 4.5, 1);
        check (near (before, 1000, .005) && near (after, 2000, .005),
               "the pitch follows the tempo map (" + juce::String (before, 1) + " Hz, then " + juce::String (after, 1) + " Hz)");

        const auto project = root.getChildFile ("Repitch").getChildFile ("Repitch.halftrack");
        ok (s.saveProject (project), "save"); ok (s.loadProject (project), "reopen");
        check (clip().warp.mode == Session::WarpMode::repitch && std::abs (clip().warp.bpm - 120) < 1e-6, "Re-Pitch reopens");
        std::cout << "Repitch passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL repitch: " << e.what() << std::endl; return 1; }
}
}
