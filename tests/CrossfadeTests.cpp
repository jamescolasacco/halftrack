#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Crossfader. Three generated sines: one on side A, one on side B and one
// unassigned. Renders at several fader positions must match the dipless law.
namespace halftrack
{
namespace
{
constexpr double rate = 48000, pi = 3.14159265358979323846;
using Side = Session::CrossfadeAssign;
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS crossfade: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
double tone (double frequency, double level, int n) { return level * std::sin (2 * pi * frequency * n / rate); }
void write (const juce::File& f, double frequency, double level)
{
    juce::AudioBuffer<float> x (2, 48000);
    for (int n = 0; n < x.getNumSamples(); ++n) for (int c = 0; c < 2; ++c) x.setSample (c, n, (float) tone (frequency, level, n));
    juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (f.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (32));
    check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write " + f.getFileName());
}
double renderError (Session& s, const juce::File& file, double gainA, double gainB)
{
    ok (s.exportWav (file, rate, 32), "render");
    juce::AudioFormatManager formats; formats.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (file));
    check (r != nullptr, "read render"); juce::AudioBuffer<float> x (2, (int) r->lengthInSamples); r->read (&x, 0, x.getNumSamples(), 0, true, true);
    double worst = 0;
    for (int n = 2000; n < 46000; ++n)
    {
        const auto expected = gainA * tone (220, .2, n) + gainB * tone (330, .2, n) + tone (440, .1, n);
        for (int c = 0; c < 2; ++c) worst = std::max (worst, std::abs (x.getSample (c, n) - expected));
    }
    return worst;
}
}
int runCrossfadeTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("crossfade-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto fa = root.getChildFile ("a.wav"), fb = root.getChildFile ("b.wav"), fc = root.getChildFile ("c.wav");
        write (fa, 220, .2); write (fb, 330, .2); write (fc, 440, .1);
        Session s (false);
        ok (s.importAudio (fa, 0), "import the A source"); ok (s.importAudio (fb, 1), "import the B source");
        ok (s.addAudioTrack ("Free"), "add an unassigned track"); ok (s.importAudio (fc, 2), "import the unassigned source");
        const auto render = root.getChildFile ("render.wav");
        check (Session::crossfadeGain (Side::a, -1) == 1 && Session::crossfadeGain (Side::a, 0) == 1 && Session::crossfadeGain (Side::a, 1) == 0
               && std::abs (Session::crossfadeGain (Side::a, .5f) - std::cos (pi / 4)) < 1e-6 && Session::crossfadeGain (Side::b, -1) == 0
               && Session::crossfadeGain (Side::none, 1) == 1, "the dipless law keeps a side at unity until the fader passes the centre");
        const auto ids = s.snapshot();
        ok (s.setTrackCrossfade (ids.tracks[0].id, Side::a), "assign the first track to A"); ok (s.setTrackCrossfade (ids.tracks[1].id, Side::b), "assign the second track to B");
        check (s.snapshot().tracks[0].crossfade == Side::a && s.snapshot().tracks[1].crossfade == Side::b && s.snapshot().tracks[2].crossfade == Side::none, "assignments are reported");
        check (renderError (s, render, 1, 1) < 1e-5, "with the fader centred every track plays at unity");
        ok (s.setCrossfader (-1), "move the fader fully to A");
        check (renderError (s, render, 1, 0) < 1e-5, "fully A silences side B and leaves the unassigned track");
        ok (s.setCrossfader (1), "move the fader fully to B");
        check (renderError (s, render, 0, 1) < 1e-5, "fully B silences side A");
        ok (s.setCrossfader (.5f), "move the fader halfway to B");
        check (renderError (s, render, std::cos (pi / 4), 1) < 1e-5, "halfway to B lowers side A along the cosine law");
        check (s.setCrossfader (std::nan ("")).failed() && s.setTrackCrossfade ("missing", Side::a).failed(), "invalid positions and tracks are rejected");
        s.undo(); check (std::abs (s.snapshot().crossfader - 1) < 1e-6, "undo restores the previous fader position");
        check (renderError (s, render, 0, 1) < 1e-5, "the undone position plays");
        s.redo();
        const auto project = root.getChildFile ("Crossfade.halftrack");
        ok (s.saveProject (project), "save the crossfade project"); ok (s.loadProject (project), "reopen it");
        check (std::abs (s.snapshot().crossfader - .5f) < 1e-6 && s.snapshot().tracks[0].crossfade == Side::a, "the fader and assignments reopen");
        check (renderError (s, render, std::cos (pi / 4), 1) < 1e-5, "the reopened project plays the saved crossfade");
        ok (s.setTrackCrossfade (s.snapshot().tracks[0].id, Side::none), "unassign the first track");
        check (renderError (s, render, 1, 1) < 1e-5, "an unassigned track ignores the fader");
        auto text = project.loadFileAsString();
        check (text.contains ("halftrackCrossfader=\""), "the project stores the fader");
        const auto broken = root.getChildFile ("Broken.halftrack");
        check (broken.replaceWithText (text.replace ("halftrackCrossfader=\"0.5\"", "halftrackCrossfader=\"5\"")), "write an out-of-range fader");
        check (s.loadProject (broken).failed() && s.snapshot().tracks[0].crossfade == Side::none, "an out-of-range fader rejects the load and keeps the open project");
        std::cout << "Crossfade passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL crossfade: " << e.what() << std::endl; return 1; }
}
}
