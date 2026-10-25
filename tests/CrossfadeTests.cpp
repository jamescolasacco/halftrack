#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Crossfader. Three generated sines: one on side A, one on side B and one
// unassigned. Renders at several fader positions must match the reference's curves,
// Constant Power by default.
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
// Constant Power, worked out afresh in double: a side at x, seen from it, before the shoulder past 0.55.
double constantPower (double x) { return std::cos ((x / 0.95 + 1) * pi / 4); }
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
        using Curve = Session::CrossfadeCurve;
        // The reference's tables: each curve sampled at 129 positions across the fader, read between them.
        check (Session::crossfadeGain (Side::a, -1) == 1 && Session::crossfadeGain (Side::a, 1) == 0 && Session::crossfadeGain (Side::b, -1) == 0
                   && Session::crossfadeGain (Side::b, 1) == 1 && Session::crossfadeGain (Side::none, 1) == 1,
               "a side plays fully at its end of the fader and not at all at the other, and an unassigned track ignores it");
        check (std::abs (Session::crossfadeGain (Side::a, 0) - std::cos (pi / 4)) < 1e-6 && std::abs (Session::crossfadeGain (Side::b, .5f) - constantPower (-.5)) < 1e-6
                   && std::abs (Session::crossfadeGain (Side::a, .5f) - constantPower (.5)) < 1e-6,
               "Constant Power, the default, gives both sides 0.7071 at the centre and follows its cosine at a table position");
        const auto knotA = constantPower (83 / 64.0 - 1), knotB = constantPower (84 / 64.0 - 1), between = knotA + .2 * (knotB - knotA);
        check (std::abs (Session::crossfadeGain (Side::a, .3f) - between) < 1e-6 && std::abs (between - constantPower (.3)) > 3e-6,
               "between table positions the gain is read along a line between them, not worked out afresh");
        const double centres[] { .5, .58210678, std::cos (pi / 4), 1, 1, 1, 1 };
        for (int c = 0; c < Session::crossfadeCurveCount; ++c)
        {
            const auto curve = static_cast<Curve> (c);
            check (std::abs (Session::crossfadeGain (Side::a, 0, curve) - centres[c]) < 1e-6 && Session::crossfadeGain (Side::a, -1, curve) == 1 && Session::crossfadeGain (Side::a, 1, curve) == 0
                       && Session::crossfadeGain (Side::a, .99f, curve) < .05f,
                   Session::crossfadeCurveName (curve) + " is full at its end, silent at the other and " + juce::String (centres[c], 4) + " at the centre");
        }
        check (std::abs (Session::crossfadeGain (Side::a, .5f, Curve::transition) - .5) < 1e-6 && Session::crossfadeGain (Side::a, .97f, Curve::fastCut) == 0
                   && Session::crossfadeGain (Side::a, .85f, Curve::fastCut) == 1, "Transition falls in a straight line past the centre, and Fast Cut holds until near the end");
        const auto ids = s.snapshot();
        check (ids.crossfadeCurve == Curve::constantPower, "a new project's crossfader curve is Constant Power, as the reference's default Set's");
        ok (s.setTrackCrossfade (ids.tracks[0].id, Side::a), "assign the first track to A"); ok (s.setTrackCrossfade (ids.tracks[1].id, Side::b), "assign the second track to B");
        check (s.snapshot().tracks[0].crossfade == Side::a && s.snapshot().tracks[1].crossfade == Side::b && s.snapshot().tracks[2].crossfade == Side::none, "assignments are reported");
        check (renderError (s, render, std::cos (pi / 4), std::cos (pi / 4)) < 1e-5, "with the fader centred the assigned tracks play at 0.7071 and the free one at unity");
        ok (s.setCrossfader (-1), "move the fader fully to A");
        check (renderError (s, render, 1, 0) < 1e-5, "fully A silences side B and leaves the unassigned track");
        ok (s.setCrossfader (1), "move the fader fully to B");
        check (renderError (s, render, 0, 1) < 1e-5, "fully B silences side A");
        ok (s.setCrossfader (.5f), "move the fader halfway to B");
        check (renderError (s, render, constantPower (.5), constantPower (-.5)) < 1e-5, "halfway to B lowers side A and raises side B along Constant Power");
        check (s.setCrossfader (std::nan ("")).failed() && s.setTrackCrossfade ("missing", Side::a).failed(), "invalid positions and tracks are rejected");
        s.undo(); check (std::abs (s.snapshot().crossfader - 1) < 1e-6, "undo restores the previous fader position");
        check (renderError (s, render, 0, 1) < 1e-5, "the undone position plays");
        s.redo();
        ok (s.setCrossfadeCurve (Curve::slowFade), "choose Slow Fade");
        check (s.snapshot().crossfadeCurve == Curve::slowFade && renderError (s, render, Session::crossfadeGain (Side::a, .5f, Curve::slowFade), 1) < 1e-5,
               "Slow Fade keeps side B at unity halfway to it and lowers side A along its curve");
        ok (s.setCrossfadeCurve (Curve::transition), "choose Transition");
        const auto range = Session::levelRange (false);
        const auto halfway = std::pow (10.0, range.convertFrom0to1 (range.convertTo0to1 (0.0) * .5) / 20);
        check (renderError (s, render, halfway, 1) < 1e-5, "Transition halves side A's fader position halfway to B, before its volume law");
        check (s.setCrossfadeCurve (static_cast<Curve> (7)).failed(), "an unknown curve is refused");
        s.undo(); check (s.snapshot().crossfadeCurve == Curve::slowFade, "undo returns the curve chosen before");
        s.redo();
        const auto project = root.getChildFile ("Crossfade.halftrack");
        ok (s.saveProject (project), "save the crossfade project"); ok (s.loadProject (project), "reopen it");
        check (std::abs (s.snapshot().crossfader - .5f) < 1e-6 && s.snapshot().tracks[0].crossfade == Side::a && s.snapshot().crossfadeCurve == Curve::transition,
               "the fader, assignments and curve reopen");
        check (renderError (s, render, halfway, 1) < 1e-5, "the reopened project plays the saved crossfade");
        ok (s.setTrackCrossfade (s.snapshot().tracks[0].id, Side::none), "unassign the first track");
        check (renderError (s, render, 1, 1) < 1e-5, "an unassigned track ignores the fader");
        auto text = project.loadFileAsString();
        check (text.contains ("halftrackCrossfader=\""), "the project stores the fader");
        const auto broken = root.getChildFile ("Broken.halftrack");
        check (broken.replaceWithText (text.replace ("halftrackCrossfader=\"0.5\"", "halftrackCrossfader=\"5\"")), "write an out-of-range fader");
        check (s.loadProject (broken).failed() && s.snapshot().tracks[0].crossfade == Side::none, "an out-of-range fader rejects the load and keeps the open project");
        check (text.contains ("halftrackCrossfadeCurve=\"6\""), "the project stores the curve");
        check (broken.replaceWithText (text.replace ("halftrackCrossfadeCurve=\"6\"", "halftrackCrossfadeCurve=\"7\"")), "write an unknown curve");
        check (s.loadProject (broken).failed() && s.snapshot().crossfadeCurve == Curve::transition, "an unknown curve rejects the load and keeps the open project");
        std::cout << "Crossfade passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL crossfade: " << e.what() << std::endl; return 1; }
}
}
