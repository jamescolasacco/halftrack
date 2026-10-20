#include "Session.h"
#include <cmath>
#include <complex>
#include <iostream>
#include <stdexcept>
#include <vector>

// Warped audio follows the project tempo. A generated click track has one
// 1 kHz burst per 120 BPM beat; renders are checked for onset positions on the
// project's beat grid, preserved pitch and exact restoration when warp is off.
namespace halftrack
{
namespace
{
using Warp = Session::WarpSettings;
using Mode = Session::WarpMode;
constexpr double pi = 3.14159265358979323846, rate = 48000;
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS warp: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
juce::AudioBuffer<float> clicks (double seconds, double spacing)
{
    juce::AudioBuffer<float> a (2, (int) (seconds * rate)); a.clear();
    for (double t = 0; t < seconds - 1e-9; t += spacing)
        for (int n = 0; n < 480; ++n)
        {
            const auto i = (int) std::lround (t * rate) + n; if (i >= a.getNumSamples()) break;
            const auto v = (float) (.5 * std::sin (2 * pi * 1000 * n / rate) * std::exp (-n / 120.0));
            a.setSample (0, i, v); a.setSample (1, i, -v);
        }
    return a;
}
void write (const juce::File& f, const juce::AudioBuffer<float>& a)
{
    juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (f.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (24));
    check (writer && writer->writeFromAudioSampleBuffer (a, 0, a.getNumSamples()), "write generated source");
}
juce::AudioBuffer<float> read (const juce::File& f)
{
    juce::AudioFormatManager formats; formats.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
    check (r != nullptr, "decode render"); juce::AudioBuffer<float> a (2, (int) r->lengthInSamples); r->read (&a, 0, a.getNumSamples(), 0, true, true); return a;
}
std::vector<double> onsets (const juce::AudioBuffer<float>& a)
{
    std::vector<double> result; const auto threshold = .25f * a.getMagnitude (0, 0, a.getNumSamples());
    int quiet = 960;
    for (int n = 0; n < a.getNumSamples(); ++n)
    {
        if (std::abs (a.getSample (0, n)) > threshold) { if (quiet >= 960) result.push_back (n / rate); quiet = 0; }
        else ++quiet;
    }
    return result;
}
// Every expected beat has exactly one onset within the tolerance.
bool onGrid (const std::vector<double>& found, double spacing, int count, double tolerance, double& worst)
{
    worst = 0;
    juce::String list; for (auto t : found) list << juce::String (t, 4) << " ";
    if ((int) found.size() != count || std::abs (found.empty() ? 1.0 : found.back() - (count - 1) * spacing) > tolerance) std::cout << "  onsets: " << list << std::endl;
    if ((int) found.size() != count) return false;
    for (int i = 0; i < count; ++i) worst = std::max (worst, std::abs (found[(size_t) i] - i * spacing));
    return worst <= tolerance;
}
double dominant (const juce::AudioBuffer<float>& a, double at)
{
    double best = 0, frequency = 0;
    for (double f = 500; f <= 1500; f += 10)
    {
        std::complex<double> sum; const int start = (int) (at * rate);
        for (int n = start; n < start + 480 && n < a.getNumSamples(); ++n) sum += (double) a.getSample (0, n) * std::polar (1.0, -2 * pi * f * n / rate);
        if (std::abs (sum) > best) { best = std::abs (sum); frequency = f; }
    }
    return frequency;
}
double difference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumSamples() != b.getNumSamples()) return 1e9;
    double worst = 0; for (int c = 0; c < 2; ++c) for (int n = 0; n < a.getNumSamples(); ++n) worst = std::max (worst, (double) std::abs (a.getSample (c, n) - b.getSample (c, n)));
    return worst;
}
Session::ClipInfo clip (Session& s, int track = 0)
{ const auto snapshot = s.snapshot(); return snapshot.tracks[(size_t) track].clips.front(); }
}
int runWarpTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("warp-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        check (Session::estimateClipTempo (4) == 120 && Session::estimateClipTempo (3) == 80 && Session::estimateClipTempo (2.5) == 96
               && Session::estimateClipTempo (1) == 120 && Session::estimateClipTempo (.75) == 80 && Session::estimateClipTempo (10) == 96,
               "tempo estimate assumes whole 4/4 bars within 80-160 BPM");
        const auto source = root.getChildFile ("clicks.wav"); write (source, clicks (4, .5));
        Session s (false); ok (s.importAudio (source, 0), "import an unwarped click track");
        auto info = clip (s);
        check (info.warp.mode == Mode::off && info.warp.bpm == 120 && info.canWarp && info.canEditAudioRange, "imported audio starts unwarped with an estimated tempo");
        const auto plain = root.getChildFile ("plain.wav"), render = root.getChildFile ("render.wav");
        ok (s.exportWav (plain), "render the unwarped reference");
        const auto reference = read (plain);
        double worst = 0;
        check (onGrid (onsets (reference), .5, 8, .001, worst), "the generated source has eight beat onsets");
        s.setTempo (60); ok (s.exportWav (render), "render unwarped audio at a new tempo");
        check (clip (s).length == 4 && difference (read (render), reference) == 0, "unwarped audio keeps its own time when the tempo changes");
        s.setTempo (120);
        check (s.setAudioClipWarp (info.id, { Mode::stretch, 10 }).failed() && clip (s).warp.mode == Mode::off, "out-of-range clip tempo is rejected");
        ok (s.setAudioClipWarp (info.id, { Mode::stretch, 120 }), "warp the clip at its own tempo");
        info = clip (s);
        check (info.warp == Warp { Mode::stretch, 120 } && std::abs (info.length - 4) < 1e-9 && ! info.canEditAudioRange && s.canUndo(),
               "warping at the project tempo keeps the clip length and locks source-time edits");
        check (s.setAudioClipRange (info.id, 0, 2, 0).failed(), "warped clips reject source-time trimming");
        ok (s.exportWav (render), "render warped at unity ratio");
        check (onGrid (onsets (read (render)), .5, 8, .02, worst), "unity-ratio warp keeps onsets on the beat (worst " + juce::String (worst * 1000, 2) + " ms)");
        for (const double tempo : { 60.0, 80.0, 90.0, 100.0, 180.0, 240.0 })
        {
            s.setTempo (tempo); info = clip (s);
            check (std::abs (info.length - 4 * 120 / tempo) < 1e-6, "warped clip length follows the tempo at " + juce::String (tempo) + " BPM");
            ok (s.exportWav (render), "render warped clip");
            const auto audio = read (render);
            check (onGrid (onsets (audio), 60 / tempo, 8, 1.0, worst), "onsets land on the " + juce::String (tempo) + " BPM grid (worst " + juce::String (worst * 1000, 2) + " ms)");
            check (std::abs (dominant (audio, 60 / tempo) - 1000) <= 30, "stretching preserves the 1 kHz pitch at " + juce::String (tempo) + " BPM");
        }
        s.setTempo (120);
        ok (s.setAudioClipWarp (info.id, { Mode::stretch, 60 }), "declare the source as 60 BPM");
        info = clip (s); ok (s.exportWav (render), "render the faster source");
        check (std::abs (info.length - 2) < 1e-6 && onGrid (onsets (read (render)), .25, 8, .02, worst), "a slower clip tempo plays twice as fast on the project grid");
        ok (s.setAudioClipWarp (info.id, { Mode::efficient, 120 }), "switch to the efficient stretch mode");
        info = clip (s); ok (s.exportWav (render), "render efficient stretch");
        check (info.warp.mode == Mode::efficient && std::abs (info.length - 4) < 1e-6 && onGrid (onsets (read (render)), .5, 8, .02, worst), "efficient mode keeps the beat grid");
        const auto project = root.getChildFile ("warp.halftrack");
        s.setTempo (90); ok (s.saveProject (project), "save warped project"); ok (s.loadProject (project), "reopen warped project");
        info = clip (s);
        check (info.warp == Warp { Mode::efficient, 120 } && std::abs (info.length - 4 * 120 / 90.0) < 1e-6, "warp mode, tempo and beat length reopen");
        s.setTempo (120);
        ok (s.setAudioClipWarp (info.id, { Mode::off, 120 }), "turn warping off");
        info = clip (s); ok (s.exportWav (render), "render after unwarping");
        check (info.warp.mode == Mode::off && std::abs (info.length - 4) < 1e-9 && info.canEditAudioRange && difference (read (render), reference) == 0,
               "unwarping restores the original source-time render exactly");
        s.undo(); info = clip (s); check (info.warp.mode == Mode::efficient, "undo restores the warped state");
        s.redo(); check (clip (s).warp.mode == Mode::off, "redo unwarps again");
        Session late (false); ok (late.importAudio (source, 0, 2), "place a clip on beat five");
        const auto lateId = clip (late).id; ok (late.setAudioClipWarp (lateId, { Mode::stretch, 120 }), "warp the late clip");
        late.setTempo (60); info = clip (late);
        check (std::abs (info.start - 4) < 1e-9 && std::abs (info.length - 8) < 1e-6, "warped clips keep their beat position across tempo changes");
        Session slots (false); ok (slots.importAudioToSlot (source, 0, 0), "import a Session clip");
        const auto slotClip = slots.snapshot().tracks[0].slots[0].clip;
        check (slotClip.warp.mode == Mode::stretch, "Session clips report that they follow the tempo");
        check (slots.setAudioClipWarp (slotClip.id, { Mode::off, 120 }).failed(), "Session clips cannot turn warping off");
        ok (slots.setAudioClipWarp (slotClip.id, { Mode::stretch, 120 }), "set the Session clip tempo");
        check (slots.snapshot().tracks[0].slots[0].clip.warp == Warp { Mode::stretch, 120 }, "Session clip tempo is reported");
        std::cout << "Warp passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL warp: " << e.what() << std::endl; return 1; }
}
}
