#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Beats, Tones, Texture and Complex Pro warping and clip transposition, rendered offline from
// generated fixtures recorded at 120 BPM and played at 60 BPM (twice as long) or 240 (half).
// Beats keeps each hit's shape and places it on the new beat; Tones and Texture keep a sine's
// pitch; Complex Pro keeps a harmonic sound's formant where Complex moves it with the pitch.
namespace halftrack
{
void useHardClipEdges();
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
juce::AudioBuffer<float> captureSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS warp modes: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
constexpr double rate = 48000;
using Mode = Session::WarpMode;
void write (const juce::File& file, const std::function<float (int)>& sample, int length)
{
    juce::AudioBuffer<float> x (2, length);
    for (int c = 0; c < 2; ++c) for (int n = 0; n < length; ++n) x.setSample (c, n, sample (n));
    juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (32));
    check (writer && writer->writeFromAudioSampleBuffer (x, 0, length), "write " + file.getFileName());
}
juce::AudioBuffer<float> render (Session& s, const juce::File& file)
{
    ok (s.exportWav (file, rate, 32, 0), "render");
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    check (reader != nullptr, "read the render");
    juce::AudioBuffer<float> x (2, (int) reader->lengthInSamples); reader->read (&x, 0, x.getNumSamples(), 0, true, true);
    return x;
}
int at (double seconds) { return (int) std::lround (seconds * rate); }
double rms (const juce::AudioBuffer<float>& x, double from, double to)
{
    double sum = 0; const int a = std::max (0, at (from)), b = std::min (x.getNumSamples(), at (to));
    for (int n = a; n < b; ++n) sum += x.getSample (0, n) * x.getSample (0, n);
    return b > a ? std::sqrt (sum / (b - a)) : 0;
}
// The frequency over a window, from its rising zero crossings.
double frequency (const juce::AudioBuffer<float>& x, double from, double seconds)
{
    const int a = at (from), b = std::min (x.getNumSamples(), a + at (seconds));
    int first = -1, last = -1, crossings = 0;
    for (int n = a + 1; n < b; ++n)
        if (x.getSample (0, n - 1) < 0 && x.getSample (0, n) >= 0) { if (first < 0) first = n; last = n; ++crossings; }
    return crossings > 1 ? (crossings - 1) * rate / (last - first) : 0;
}
// The magnitude of one frequency over a window (a single DFT bin).
double magnitude (const juce::AudioBuffer<float>& x, double hz, double from, double seconds)
{
    double re = 0, im = 0; const int a = at (from), b = std::min (x.getNumSamples(), a + at (seconds));
    for (int n = a; n < b; ++n)
    {
        const auto phase = juce::MathConstants<double>::twoPi * hz * n / rate;
        re += x.getSample (0, n) * std::cos (phase); im += x.getSample (0, n) * std::sin (phase);
    }
    return std::sqrt (re * re + im * im) / std::max (1, b - a);
}
// A frequency's magnitude averaged over 40 ms windows, which grains with wandering phases leave intact.
double windowed (const juce::AudioBuffer<float>& x, double hz, double from, double seconds)
{
    double sum = 0; int windows = 0;
    for (double t = from; t + .04 <= from + seconds; t += .04, ++windows) sum += magnitude (x, hz, t, .04);
    return windows > 0 ? sum / windows : 0;
}
// The first sample after a time where the signal passes a level.
double onsetAfter (const juce::AudioBuffer<float>& x, double from, float level)
{
    for (int n = std::max (0, at (from)); n < x.getNumSamples(); ++n) if (std::abs (x.getSample (0, n)) > level) return n / rate;
    return -1;
}
bool near (double a, double b, double relative) { return std::abs (a - b) <= std::abs (b) * relative; }
}

int runWarpModeTests (const juce::File& artifacts)
{
    try
    {
        useHardClipEdges(); // Renders here are compared sample for sample with hard clip edges.
        const auto root = artifacts.getChildFile ("warp-modes-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto sine = root.getChildFile ("sine.wav"), hits = root.getChildFile ("hits.wav"), vowel = root.getChildFile ("vowel.wav");
        write (sine, [] (int n) { return (float) (.5 * std::sin (juce::MathConstants<double>::twoPi * 1000 * n / rate)); }, at (4));
        // A decaying 1 kHz hit on every beat at 120 BPM.
        write (hits, [] (int n)
        {
            const auto t = std::fmod (n / rate, .5);
            return (float) (.8 * std::exp (-t / .1) * std::sin (juce::MathConstants<double>::twoPi * 1000 * t));
        }, at (4));
        // Harmonics of 200 Hz under one formant at 1 kHz.
        write (vowel, [] (int n)
        {
            double v = 0;
            for (int k = 1; k <= 30; ++k) { const auto f = 200.0 * k; v += std::exp (-std::pow ((f - 1000) / 200, 2)) * std::sin (juce::MathConstants<double>::twoPi * f * n / rate); }
            return (float) (.3 * v);
        }, at (4));
        const auto out = root.getChildFile ("out.wav");
        Session s (false);
        ok (s.importAudio (hits, 0, 0), "import the hits");
        const auto clip = [&] { return s.snapshot().tracks[0].clips[0]; };
        const auto id = clip().id;

        // Settings: each mode reads back its own, and out-of-range values are refused.
        Session::WarpSettings beats { Mode::beats, 120 };
        ok (s.setAudioClipWarp (id, beats), "warp the hits in Beats at 120 BPM");
        check (clip().warp == beats && clip().warp.transientLoop == Session::TransientLoop::off && clip().warp.transientEnvelope == 100, "Beats reads back with its defaults");
        for (auto [mode, name] : { std::pair { Mode::tones, "Tones" }, { Mode::texture, "Texture" }, { Mode::complexPro, "Complex Pro" } })
        {
            Session::WarpSettings settings { mode, 120 };
            if (mode != Mode::complexPro) settings.grainSize = 40;
            if (mode == Mode::texture) settings.flux = 25;
            if (mode == Mode::complexPro) settings.formants = 60;
            ok (s.setAudioClipWarp (id, settings), juce::String ("choose ") + name);
            check (clip().warp == settings, juce::String (name) + " reads back its settings");
        }
        auto bad = beats; bad.transientEnvelope = 120;
        check (s.setAudioClipWarp (id, bad).failed(), "an envelope over 100 is refused");
        bad = { Mode::tones, 120 }; bad.grainSize = .5f;
        check (s.setAudioClipWarp (id, bad).failed() && clip().warp.mode == Mode::complexPro, "a grain size below 1 is refused, leaving the clip as it was");
        auto other = beats; other.flux = 30;
        ok (s.setAudioClipWarp (id, other), "another mode's setting");
        check (clip().warp == beats, "is ignored");

        // Beats at 60 BPM: each hit plays as recorded, starting on its new beat a second apart.
        s.setTempo (60);
        auto x = render (s, out);
        juce::String starts; bool placed = true, shaped = true;
        for (int k = 0; k < 8; ++k)
        {
            const auto start = onsetAfter (x, k - .02, .05f);
            starts << juce::String (start, 4) << " ";
            placed = placed && std::abs (start - k) < .003;
            shaped = shaped && near (rms (x, k, k + .1), rms (x, 0, .1), .05);
        }
        check (placed, "the hits start on the 60 BPM beats (" + starts.trim() + ")");
        check (shaped && near (rms (x, .05, .1), .8 * std::exp (-.75) / std::sqrt (2.0), .1), "each hit keeps its shape and level");
        check (rms (x, 2.6, 2.95) < 1e-3, "with Transient Loop off, each runs out into silence before the next");
        beats.transientLoop = Session::TransientLoop::forward; ok (s.setAudioClipWarp (id, beats), "loop forward");
        x = render (s, out);
        check (rms (x, 2.6, 2.95) > 3e-3 && std::abs (onsetAfter (x, 3 - .02, .05f) - 3) < .003, "looping forward fills the gap and keeps the beats");
        beats.transientLoop = Session::TransientLoop::off; beats.transientEnvelope = 20; ok (s.setAudioClipWarp (id, beats), "a short transient envelope");
        x = render (s, out);
        check (rms (x, 2.25, 2.4) < rms (x, 0, .1) * .05 && near (rms (x, 2, 2.02), rms (render (s, out), 2, 2.02), 1e-6), "segments fade out early, the same on every render");
        beats.transientEnvelope = 100; ok (s.setAudioClipWarp (id, beats), "back to the full envelope");
        s.setTempo (240);
        x = render (s, out);
        juce::String quick; bool quickPlaced = true;
        // Each cut-short hit is still sounding just before the next, so look from 5 ms before at a higher level.
        for (int k = 0; k < 8; ++k) { const auto start = onsetAfter (x, k * .25 - .005, .2f); quick << juce::String (start, 4) << " "; quickPlaced = quickPlaced && std::abs (start - k * .25) < .003; }
        check (quickPlaced, "at 240 BPM the hits are cut short and start a quarter second apart (" + quick.trim() + ")");
        s.setTempo (120);

        // Tones and Texture keep a sine's pitch at half speed.
        ok (s.removeClip (0, 0), "replace the hits"); ok (s.importAudio (sine, 0, 0), "with the sine");
        const auto sineId = clip().id;
        s.setTempo (60);
        for (auto [mode, name] : { std::pair { Mode::tones, "Tones" }, { Mode::texture, "Texture" } })
        {
            ok (s.setAudioClipWarp (sineId, { mode, 120 }), juce::String ("warp the sine in ") + name);
            x = render (s, out);
            const auto pitch = magnitude (x, 1000, 2, 2), sideBand = std::max (magnitude (x, 900, 2, 2), magnitude (x, 1100, 2, 2));
            check (std::abs (clip().length - 8) < 1e-3 && pitch > .1 && pitch > sideBand * 20 && rms (x, 7, 7.5) > .05,
                   juce::String (name) + " plays it for 8 s at 1 kHz (" + juce::String (pitch, 3) + " at 1 kHz, " + juce::String (sideBand, 4) + " beside)");
        }
        Session::WarpSettings texture { Mode::texture, 120 }; texture.flux = 60;
        ok (s.setAudioClipWarp (sineId, texture), "Texture with Flux");
        x = render (s, out);
        const auto again = render (s, out);
        bool same = x.getNumSamples() == again.getNumSamples();
        for (int n = 0; same && n < x.getNumSamples(); n += 97) same = x.getSample (0, n) == again.getSample (0, n);
        const auto fluxPitch = windowed (x, 1000, 2, 2), fluxSide = std::max (windowed (x, 900, 2, 2), windowed (x, 1100, 2, 2));
        check (same && fluxPitch > fluxSide * 4, "Flux wanders the grains the same way on every render, keeping the pitch ("
                   + juce::String (fluxPitch, 3) + " at 1 kHz, " + juce::String (fluxSide, 3) + " beside in 40 ms windows)");

        // Transposition keeps the speed in the stretching modes.
        ok (s.setAudioClipWarp (sineId, { Mode::stretch, 120 }), "warp the sine in Complex");
        auto audio = clip().audio; audio.transposeSemitones = 12;
        ok (s.setAudioClipSettings (sineId, audio), "transpose it up an octave");
        x = render (s, out);
        check (near (frequency (x, 2, 2), 2000, .01) && std::abs (clip().length - 8) < 1e-3, "it sounds 2 kHz, still 8 s long (" + juce::String (frequency (x, 2, 2), 1) + " Hz)");
        audio.transposeSemitones = 0; audio.detuneCents = 50;
        ok (s.setAudioClipSettings (sineId, audio), "detune it 50 cents");
        check (near (frequency (render (s, out), 2, 2), 1000 * std::pow (2.0, 50 / 1200.0), .005), "it sounds 1029 Hz");
        audio.transposeSemitones = 49;
        check (s.setAudioClipSettings (sineId, audio).failed(), "transposing past 48 semitones is refused");
        ok (s.setAudioClipWarp (sineId, { Mode::repitch, 120 }), "switch to Re-Pitch");
        check (clip().audio.transposeSemitones == 0 && clip().audio.detuneCents == 0, "Re-Pitch clears the transposition");
        audio = clip().audio; audio.transposeSemitones = 3;
        check (s.setAudioClipSettings (sineId, audio).failed(), "and refuses it, as does an unwarped clip");
        s.setTempo (120);

        // Complex Pro keeps the formant where Complex moves it with the pitch.
        ok (s.removeClip (0, 0), "replace the sine"); ok (s.importAudio (vowel, 0, 0), "with the harmonic sound");
        const auto vowelId = clip().id;
        const auto formant = [&] (const juce::AudioBuffer<float>& y)
        {
            double best = 0, at = 0;
            for (int k = 1; k <= 12; ++k) if (const auto m = magnitude (y, 400.0 * k, 1, 2); m > best) { best = m; at = 400.0 * k; }
            return at;
        };
        for (auto [mode, formants, low, name] : { std::tuple { Mode::stretch, 100.0f, false, "Complex" }, { Mode::complexPro, 100.0f, true, "Complex Pro" },
                                                  { Mode::complexPro, 0.0f, false, "Complex Pro with no formants kept" } })
        {
            Session::WarpSettings settings { mode, 120 }; settings.formants = formants;
            ok (s.setAudioClipWarp (vowelId, settings), juce::String ("warp it in ") + name);
            auto transposed = clip().audio; transposed.transposeSemitones = 12;
            ok (s.setAudioClipSettings (vowelId, transposed), "an octave up");
            x = render (s, out);
            const auto peak = formant (x);
            // An octave up, the harmonics fall on multiples of 400 Hz, not the odd multiples of 200 between.
            double on = 0, between = 0;
            for (int m = 1; m <= 8; ++m) { on += magnitude (x, 400.0 * m, 1, 2); between += magnitude (x, 200.0 * (2 * m - 1), 1, 2); }
            check (on > between * 4 && (low ? peak < 1500 : peak > 1500),
                   juce::String (name) + ": the harmonics are an octave up and the strongest is at " + juce::String (peak) + " Hz");
        }

        // Saved and reopened; a malformed setting in a file is refused.
        Session::WarpSettings saved { Mode::texture, 120 }; saved.grainSize = 50; saved.flux = 10;
        ok (s.setAudioClipWarp (vowelId, saved), "Texture with its settings");
        const auto project = root.getChildFile ("Warp").getChildFile ("Warp.halftrack");
        ok (s.saveProject (project), "save");
        {
            Session reopened (false);
            ok (reopened.loadProject (project), "reopen");
            check (reopened.snapshot().tracks[0].clips[0].warp == saved && reopened.snapshot().tracks[0].clips[0].audio.transposeSemitones == 12, "the mode, its settings and the transposition reopen");
            const auto text = project.loadFileAsString();
            const auto badFile = root.getChildFile ("Bad").getChildFile ("Bad.halftrack");
            check (text.contains ("elastiqueOptions=\"2/") && badFile.getParentDirectory().createDirectory().wasOk()
                       && badFile.replaceWithText (text.replace ("elastiqueOptions=\"2/0/0/0/64/50", "elastiqueOptions=\"2/0/0/0/64/500")), "write a grain size of 500");
            check (reopened.loadProject (badFile).failed() && reopened.snapshot().tracks[0].clips[0].warp == saved, "it is refused, keeping the open project");
        }

        // Realtime playback of a Beats clip.
        {
            Session live (false, makeSyntheticTestDeviceType());
            ok (live.importAudio (hits, 0, 0), "import the hits for playback");
            ok (live.setAudioClipWarp (live.snapshot().tracks[0].clips[0].id, { Mode::beats, 120 }), "in Beats");
            live.setTempo (60);
            pumpSyntheticTestDevice (live, 8);
            live.seek (0); ok (live.play(), "play");
            pumpSyntheticTestDevice (live, 40);
            const auto heard = captureSyntheticTestDevice (live, 400);
            check (heard.getMagnitude (0, 0, heard.getNumSamples()) > .3f, "it plays live");
        }
        std::cout << "Warp modes passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL warp modes: " << e.what() << std::endl; return 1; }
}
}
