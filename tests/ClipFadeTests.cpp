#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Default clip fades, after the reference's Create Fades on Clip Edges: new audio clips fade in and
// out over 4 ms (192 samples at 48 kHz) with the smoothstep 3x^2 - 2x^3; a split's two new edges are
// default too, and two touching clips with default edges crossfade over the earlier one's last 4 ms,
// the later one playing the material before its start, so a split plays back as the unsplit clip.
// A fade given a length or curve is an ordinary fade; the preference off leaves hard edges. Renders
// of a constant level and of a sine are read sample by sample.
namespace halftrack
{
namespace
{
constexpr double rate = 48000, pi = 3.14159265358979323846;
constexpr int fadeSamples = 192;
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS clip fades: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
double sine (int n) { return .5 * std::sin (2 * pi * 997 * n / rate); }
void write (const juce::File& f, bool constant)
{
    juce::AudioBuffer<float> x (1, 48000);
    for (int n = 0; n < x.getNumSamples(); ++n) x.setSample (0, n, (float) (constant ? .5 : sine (n)));
    juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (f.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (1).withBitsPerSample (32));
    check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write " + f.getFileName());
}
juce::AudioBuffer<float> render (Session& s, const juce::File& file)
{
    ok (s.exportWav (file, rate, 32), "render");
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (file));
    check (r != nullptr, "read the render");
    juce::AudioBuffer<float> x ((int) r->numChannels, (int) r->lengthInSamples); r->read (&x, 0, x.getNumSamples(), 0, true, true);
    return x;
}
double smoothstep (double x) { x = juce::jlimit (0.0, 1.0, x); return x * x * (3 - 2 * x); }
Session::ClipInfo clipOn (Session& s, int track, int index) { const auto now = s.snapshot(); return now.tracks[(size_t) track].clips[(size_t) index]; }
}

int runClipFadeTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("clip-fades-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto level = root.getChildFile ("level.wav"), tone = root.getChildFile ("tone.wav"), out = root.getChildFile ("render.wav");
        write (level, true); write (tone, false);
        Session s (false);
        check (Session::Preferences {}.createFadesOnClipEdges && s.preferences().createFadesOnClipEdges, "Create Fades on Clip Edges is on by default, as in the reference");

        // A new clip's edges are default fades.
        ok (s.importAudio (level, 0, .25), "import a constant level at 0.25 s");
        auto clip = clipOn (s, 0, 0);
        check (clip.audio.defaultFadeIn && clip.audio.defaultFadeOut && std::abs (clip.audio.fadeInSeconds - .004) < 1e-9 && std::abs (clip.audio.fadeOutSeconds - .004) < 1e-9
                   && clip.audio.fadeInCurve == Session::FadeCurve::smoothstep && clip.audio.fadeOutCurve == Session::FadeCurve::smoothstep,
               "its edges are default fades, reported as 4 ms smoothstep fades");
        auto x = render (s, out);
        const int start = 12000, end = 12000 + 48000;
        double worstIn = 0, worstOut = 0, worstMiddle = 0;
        for (int i = 0; i <= fadeSamples; ++i)
        {
            worstIn = std::max (worstIn, std::abs (x.getSample (0, start + i) - .5 * smoothstep ((double) i / fadeSamples)));
            worstOut = std::max (worstOut, std::abs (x.getSample (0, end - fadeSamples + i) - .5 * smoothstep (1 - (double) i / fadeSamples)));
        }
        for (int n = start + fadeSamples; n < end - fadeSamples; ++n) worstMiddle = std::max (worstMiddle, std::abs (x.getSample (0, n) - .5));
        check (std::abs (x.getSample (0, start - 1)) < 1e-9 && worstIn < 2e-3, "it fades in over 192 samples along the smoothstep (worst " + juce::String (worstIn, 6) + ")");
        check (worstOut < 2e-3 && std::abs (x.getSample (0, end + 1)) < 1e-9, "and fades out over its last 192 samples (worst " + juce::String (worstOut, 6) + ")");
        check (worstMiddle < 1e-6, "and plays at its level between");

        // A split's edges crossfade, so the material plays on unchanged.
        s.undo();
        ok (s.importAudio (tone, 0, .25), "import a sine at 0.25 s instead");
        juce::String right;
        ok (s.splitAudioClip (clipOn (s, 0, 0).id, .75, right), "split it at 0.75 s");
        check (clipOn (s, 0, 0).audio.defaultFadeOut && clipOn (s, 0, 1).audio.defaultFadeIn, "the split's two edges are default fades");
        x = render (s, out);
        double worstSplit = 0;
        for (int n = start + fadeSamples; n < end - fadeSamples; ++n) worstSplit = std::max (worstSplit, std::abs (x.getSample (0, n) - sine (n - start)));
        check (worstSplit < 1e-5, "across the split the two crossfade back into the unsplit sine (worst " + juce::String (worstSplit, 8) + ")");
        const int boundary = 36000;
        check (std::abs (x.getSample (0, boundary - fadeSamples / 2) - sine (boundary - fadeSamples / 2 - start)) < 1e-5, "including half way through the crossfade, before the boundary");

        // An ordinary fade, and back.
        auto settings = clipOn (s, 0, 1).audio;
        settings.fadeInSeconds = .1; settings.fadeInCurve = Session::FadeCurve::linear;
        ok (s.setAudioClipSettings (right, settings), "give the right part a 100 ms linear fade-in");
        check (! clipOn (s, 0, 1).audio.defaultFadeIn && std::abs (clipOn (s, 0, 1).audio.fadeInSeconds - .1) < 1e-9 && clipOn (s, 0, 1).audio.fadeInCurve == Session::FadeCurve::linear,
               "a length set on a default edge makes it an ordinary fade");
        x = render (s, out);
        check (std::abs (x.getSample (0, boundary + 2400) - .5 * sine (boundary + 2400 - start)) < 1e-4 && std::abs (x.getSample (0, boundary - fadeSamples / 2) - .5 * sine (boundary - fadeSamples / 2 - start)) < 1e-4,
               "it fades in linearly from the boundary, and the left part's default fade-out no longer crossfades");
        settings = clipOn (s, 0, 1).audio; settings.defaultFadeIn = true;
        ok (s.setAudioClipSettings (right, settings), "make it a default fade again");
        check (clipOn (s, 0, 1).audio.defaultFadeIn && std::abs (clipOn (s, 0, 1).audio.fadeInSeconds - .004) < 1e-9, "it is a default fade again");
        s.undo();
        check (! clipOn (s, 0, 1).audio.defaultFadeIn, "undo restores the ordinary fade");
        s.redo();

        // Saved with the project.
        ok (s.saveProject (root.getChildFile ("Fades").getChildFile ("Fades.halftrack")), "save");
        {
            Session reopened (false);
            ok (reopened.loadProject (s.snapshot().projectFile), "reopen");
            check (clipOn (reopened, 0, 0).audio.defaultFadeOut && clipOn (reopened, 0, 1).audio.defaultFadeIn, "default fades are saved with the clips");
        }

        // A clip too short for two 4 ms fades shares its length between them.
        ok (s.newProject(), "a new project");
        ok (s.importAudio (level, 0, 0), "import the level");
        ok (s.setAudioClipRange (clipOn (s, 0, 0).id, 0, .006, 0), "trim it to 6 ms");
        check (std::abs (clipOn (s, 0, 0).audio.fadeInSeconds - .003) < 1e-9 && std::abs (clipOn (s, 0, 0).audio.fadeOutSeconds - .003) < 1e-9, "each default fade takes 3 ms");

        // With the preference off, new clips and splits have hard edges.
        auto prefs = s.preferences(); prefs.createFadesOnClipEdges = false; ok (s.setPreferences (prefs), "switch Create Fades on Clip Edges off");
        ok (s.newProject(), "a new project");
        ok (s.importAudio (level, 0, .25), "import the level again");
        clip = clipOn (s, 0, 0);
        check (! clip.audio.defaultFadeIn && ! clip.audio.defaultFadeOut && clip.audio.fadeInSeconds == 0, "the new clip has no default fades");
        x = render (s, out);
        check (std::abs (x.getSample (0, start) - .5) < 1e-6 && std::abs (x.getSample (0, end - 1) - .5) < 1e-6, "and plays at full level from its first sample to its last");
        prefs.createFadesOnClipEdges = true; ok (s.setPreferences (prefs), "switch it on again");
        root.deleteRecursively();
        std::cout << "Clip fades passed" << std::endl;
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL clip fades: " << e.what() << std::endl; return 1; }
}
}
