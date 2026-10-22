#include "Session.h"
#include "TempoDetection.h"
#include "WarpStretchers.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>

// Tempo and first-beat analysis against generated drum loops whose tempo and first beat are known,
// cut to lengths whose whole-bar guess is wrong, then a long sample warped by it on import.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
namespace
{
void ok (const juce::Result& r, const juce::String& text);
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS tempo analysis: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
void write (const juce::File& file, const juce::AudioBuffer<float>& audio, double rate)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (24));
    if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples())) throw std::runtime_error ("could not write " + file.getFileName().toStdString());
}

struct Loop
{
    double bpm = 120, seconds = 10, lead = 0, rate = 48000, swing = 0;
    int introBars = 0; // Bars of chords alone before the drums.
    bool pad = false;   // A held chord under the drums, changing each bar.
};

// Kick on 1 and 3, snare on 2 and 4, closed hats on the eighths, after `lead` seconds of silence.
juce::AudioBuffer<float> drumLoop (const Loop& loop)
{
    const int length = (int) (loop.seconds * loop.rate);
    juce::AudioBuffer<float> out (2, length);
    out.clear();
    juce::Random random (97);
    const double beat = 60 / loop.bpm;
    const auto add = [&] (double at, double duration, auto&& voice)
    {
        const int from = (int) std::lround (at * loop.rate), to = juce::jmin (length, from + (int) (duration * loop.rate));
        for (int n = juce::jmax (0, from); n < to; ++n)
        {
            const float v = voice ((n - from) / loop.rate);
            out.addSample (0, n, v); out.addSample (1, n, v);
        }
    };
    const auto kick = [&] (double t)
    {
        const double phase = juce::MathConstants<double>::twoPi * (50 * t + 100 * 0.03 * (1 - std::exp (-t / 0.03)));
        return (float) (0.8 * std::exp (-t / 0.12) * std::sin (phase));
    };
    const auto snare = [&] (double t) { return (float) (0.4 * std::exp (-t / 0.06) * random.nextFloat() * 2 - 0.4 * std::exp (-t / 0.06) + 0.3 * std::exp (-t / 0.05) * std::sin (juce::MathConstants<double>::twoPi * 180 * t)); };
    float last = 0;
    const auto hat = [&] (double t) { const float noise = random.nextFloat() * 2 - 1, v = noise - last; last = noise; return (float) (0.15 * std::exp (-t / 0.015) * v); };
    const int beatsPerBar = 4;
    for (int b = 0; loop.lead + b * beat < loop.seconds; ++b)
    {
        const double t = loop.lead + b * beat;
        if (loop.pad && b % beatsPerBar == 0)
        {
            const double root = 110 * std::pow (2.0, (b / beatsPerBar % 4) * 3 / 12.0);
            add (t, beat * beatsPerBar, [&] (double s)
            {
                const double attack = juce::jmin (1.0, s / 0.005);
                return (float) (0.06 * attack * (std::sin (juce::MathConstants<double>::twoPi * root * s) + std::sin (juce::MathConstants<double>::twoPi * root * 1.26 * s)
                                                 + std::sin (juce::MathConstants<double>::twoPi * root * 1.5 * s)));
            });
        }
        if (b < loop.introBars * beatsPerBar) continue;
        if (b % 2 == 0) add (t, 0.4, kick); else add (t, 0.25, snare);
        add (t, 0.08, hat);
        add (t + beat * (0.5 + loop.swing), 0.08, hat);
    }
    return out;
}

juce::String describe (const Loop& loop)
{
    return juce::String (loop.bpm, 2) + " BPM over " + juce::String (loop.seconds, 1) + " s at " + juce::String (loop.rate / 1000, 1) + " kHz";
}
}

int runTempoAnalysisTests (const juce::File& artifacts)
{
    try
    {
        // Each loop's length gives the wrong tempo when taken for whole 4/4 bars.
        struct Case { Loop loop; double expected; };
        const Case cases[] = {
            { { 97, 9.1, .237 }, 97 },
            { { 84.5, 12.9, .05 }, 84.5 },
            { { 112.3, 13.7, .6, 48000, .08 }, 112.3 },
            { { 123, 8.2, 0 }, 123 },
            { { 136.7, 17.9, .12 }, 136.7 },
            { { 145, 10.6, .4 }, 145 },
            { { 152, 7.7, .02 }, 152 },
            { { 128, 31.4, .31, 48000, 0, 0, true }, 128 },
            { { 118.4, 20, .2, 44100 }, 118.4 },
            { { 101.5, 12, .15, 96000 }, 101.5 },
            // Outside 80-160, the double or the half.
            { { 70, 15, .1 }, 140 },
            { { 172, 12, .1 }, 86 },
        };
        // Every loop is reported before a miss fails the suite.
        juce::StringArray misses;
        for (const auto& c : cases)
        {
            const auto result = tempo::estimate (drumLoop (c.loop), c.loop.rate);
            const auto lengthGuess = Session::estimateClipTempo (c.loop.seconds);
            const bool found = result && std::abs (result->bpm - c.expected) <= .05 && std::abs (result->firstBeatSeconds - c.loop.lead) < .002 && std::abs (lengthGuess - c.expected) > 1;
            const auto text = describe (c.loop) + ": " + (result ? juce::String (result->bpm, 2) + " BPM, first beat " + juce::String ((result->firstBeatSeconds - c.loop.lead) * 1000, 2)
                                                                       + " ms from it (comb " + juce::String (result->confidence, 2) + "x), where its length says " + juce::String (lengthGuess, 2)
                                                                    : juce::String ("no tempo"));
            std::cout << (found ? "PASS" : "MISS") << " tempo analysis: " << text << std::endl;
            if (! found) misses.add (text);
        }
        check (misses.isEmpty(), "every loop's tempo within 0.05 BPM and first beat within 2 ms" + (misses.isEmpty() ? juce::String() : ", but " + misses.joinIntoString ("; ")));
        {
            // A three-minute song: eight bars of chords, then drums.
            const Loop song { 126.5, 180, .43, 48000, 0, 8, true };
            const auto audio = drumLoop (song);
            const auto started = std::chrono::steady_clock::now();
            const auto result = tempo::estimate (audio, song.rate);
            const auto took = std::chrono::duration<double> (std::chrono::steady_clock::now() - started).count();
            check (result && std::abs (result->bpm - 126.5) <= .02 && std::abs (result->firstBeatSeconds - song.lead) < .015,
                   "a three-minute song with a chord intro: " + (result ? juce::String (result->bpm, 2) + " BPM, first beat " + juce::String ((result->firstBeatSeconds - song.lead) * 1000, 2) + " ms from it"
                                                                      : juce::String ("no tempo")) + ", in " + juce::String (took, 2) + " s");
            check (took < 3, "the analysis is quick enough to run on import");
        }
        {
            juce::AudioBuffer<float> noise (2, 48000 * 20), tone (2, 48000 * 20);
            juce::Random random (3);
            for (int n = 0; n < noise.getNumSamples(); ++n)
            {
                const float v = 0.3f * (random.nextFloat() * 2 - 1);
                noise.setSample (0, n, v); noise.setSample (1, n, v);
                tone.setSample (0, n, 0.3f * (float) std::sin (n * 0.05)); tone.setSample (1, n, tone.getSample (0, n));
            }
            const auto fromNoise = tempo::estimate (noise, 48000), fromTone = tempo::estimate (tone, 48000);
            check (! fromNoise && ! fromTone, "noise and a held tone have no tempo"
                   + (fromNoise ? " (noise gave " + juce::String (fromNoise->bpm, 2) + " at " + juce::String (fromNoise->confidence, 2) + "x)" : juce::String())
                   + (fromTone ? " (the tone gave " + juce::String (fromTone->bpm, 2) + " at " + juce::String (fromTone->confidence, 2) + "x)" : juce::String()));
            check (! tempo::estimate (drumLoop ({ 120, 4, 0 }), 48000), "a sound shorter than eight slow beats has no tempo");
        }
        const auto root = artifacts.getChildFile ("tempo-analysis-" + juce::Uuid().toString());
        check (root.createDirectory().wasOk(), "create fixtures");
        {
            // Read from a file a block at a time, as the same estimate.
            const Loop loop { 97, 9.1, .237 };
            const auto file = root.getChildFile ("loop.wav");
            write (file, drumLoop (loop), loop.rate);
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(), true));
            const auto fromFile = reader ? tempo::estimate (*reader) : std::nullopt;
            const auto fromBuffer = tempo::estimate (drumLoop (loop), loop.rate);
            check (fromFile && fromBuffer && std::abs (fromFile->bpm - fromBuffer->bpm) < .011 && std::abs (fromFile->firstBeatSeconds - fromBuffer->firstBeatSeconds) < .0005,
                   "a file read a block at a time gives the same tempo and first beat");
            reader.reset();
        }
        {
            // Auto-Warp Long Samples: a 40-second loop at 97 BPM after 0.6 s of silence.
            const Loop loop { 97, 40, .6 };
            const auto file = root.getChildFile ("long loop.wav"), noiseFile = root.getChildFile ("long noise.wav");
            write (file, drumLoop (loop), loop.rate);
            {
                juce::AudioBuffer<float> noise (2, 48000 * 40);
                juce::Random random (5);
                for (int n = 0; n < noise.getNumSamples(); ++n) { const float v = 0.2f * (random.nextFloat() * 2 - 1); noise.setSample (0, n, v); noise.setSample (1, n, v); }
                write (noiseFile, noise, 48000);
            }
            Session s (false, makeSyntheticTestDeviceType());
            ok (s.saveProject (root.getChildFile ("Warp").getChildFile ("Warp.halftrack")), "save a project");
            // Beats, as for drums, plays each transient where it falls.
            auto preferences = s.preferences(); preferences.autoWarpLongSamples = true; preferences.defaultWarpMode = Session::WarpMode::beats;
            ok (s.setPreferences (preferences), "turn on Auto-Warp Long Samples, warping in Beats");
            ok (s.addAudioTrack ("Loop"), "add a track");
            ok (s.importAudio (file, 0, 0), "import the loop");
            const auto clip = s.snapshot().tracks[0].clips[0];
            const auto tempo = s.snapshot().tempo;
            check (clip.warp.mode != Session::WarpMode::off && std::abs (clip.warp.bpm - 97) <= .05,
                   "it is warped at its own tempo, " + juce::String (clip.warp.bpm, 2) + " BPM, not its length's " + juce::String (Session::estimateClipTempo (40), 2));
            check (clip.warpMarkers.size() == 2 && std::abs (clip.warpMarkers[1].sourceSeconds - loop.lead) < .002 && std::abs (clip.warpMarkers[1].beat - clip.warpMarkers[1].sourceSeconds * clip.warp.bpm / 60) < 1e-6,
                   "a warp marker pins its first beat, at its own tempo");
            check (std::abs (clip.sourceOffset - loop.lead * clip.warp.bpm / tempo) < .002 && clip.start == 0,
                   "the clip starts on that beat");
            const auto transients = s.audioClipTransients (clip.id);
            int onBeats = 0;
            for (int b = 0; b < 16; ++b)
                for (const auto t : transients) if (std::abs (t - (loop.lead + b * 60 / loop.bpm)) < .001) { ++onBeats; break; }
            check (onBeats == 16, "the clip's transients fall on the loop's beats (" + juce::String (onBeats) + " of the first 16)");
            // Played, its beats fall on the project's. The first sounds at once; the detector counts the
            // render's start as a transient, so the others are looked for.
            const auto rendered = root.getChildFile ("rendered.wav");
            ok (s.exportWav (rendered), "render it");
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (rendered.createInputStream().release(), true));
            check (reader != nullptr, "read the render");
            juce::AudioBuffer<float> heard (2, (int) std::min<juce::int64> (reader->lengthInSamples, 48000 * 10));
            reader->read (&heard, 0, heard.getNumSamples(), 0, true, true);
            const auto found = warp::findTransients (heard, reader->sampleRate);
            double worst = 0;
            int placed = 0;
            check (heard.getMagnitude (0, 240) > .1f, "the first beat sounds at the start");
            for (int b = 1; b < 16; ++b)
            {
                const double expected = b * 60 / tempo;
                double nearest = 1;
                for (const auto f : found) nearest = std::min (nearest, std::abs (f / reader->sampleRate - expected));
                if (nearest < .003) ++placed;
                worst = std::max (worst, nearest);
            }
            check (placed == 15, "the next 15 sound on the project's beats, at worst " + juce::String (worst * 1000, 2) + " ms off");
            s.undo();
            check (s.snapshot().tracks[0].clips.empty(), "the import and its warping are one undo step");
            ok (s.importAudio (noiseFile, 0, 0), "import 40 s of noise");
            const auto noise = s.snapshot().tracks[0].clips[0];
            check (std::abs (noise.warp.bpm - Session::estimateClipTempo (40)) < 1e-6 && noise.warpMarkers.empty() && noise.sourceOffset == 0,
                   "with no beat to find, it takes the tempo of whole bars, with no marker");
            ok (s.resetPreferences(), "reset the preferences");
        }
        root.deleteRecursively();
        std::cout << "Tempo analysis passed" << std::endl;
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL tempo analysis: " << e.what() << std::endl; return 1; }
}

// Manual measurement on a library whose loop names carry their tempo ("... 115 bpm.wav"): how often
// the content estimate and the length guess find it, the name's tempo doubled or halved into
// 80-160. Prints a summary and, given a report path, writes each loop's result as JSON.
int runTempoAnalysisEvaluation (const juce::File& library, const juce::File& report)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    juce::Array<juce::var> rows;
    int loops = 0, found = 0, byLength = 0, none = 0, octave = 0;
    double seconds = 0;
    for (const auto& entry : juce::RangedDirectoryIterator (library, true, "*.wav;*.aif;*.aiff;*.flac", juce::File::findFiles))
    {
        const auto name = entry.getFile().getFileNameWithoutExtension();
        const auto words = juce::StringArray::fromTokens (name, " ", "");
        const int at = words.indexOf ("bpm", true);
        if (at < 1 || words[at - 1].getDoubleValue() <= 0) continue;
        double named = words[at - 1].getDoubleValue();
        while (named < 80) named *= 2;
        while (named >= 160) named /= 2;
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (entry.getFile()));
        if (reader == nullptr || reader->sampleRate <= 0) continue;
        const auto length = (double) reader->lengthInSamples / reader->sampleRate;
        const auto started = std::chrono::steady_clock::now();
        const auto result = tempo::estimate (*reader);
        seconds += std::chrono::duration<double> (std::chrono::steady_clock::now() - started).count();
        const auto guess = Session::estimateClipTempo (length);
        ++loops;
        const bool hit = result && std::abs (result->bpm - named) <= .5;
        if (hit) ++found;
        if (std::abs (guess - named) <= .5) ++byLength;
        if (! result) ++none;
        else if (! hit)
            for (const double ratio : { 2.0, .5, 1.5, 2.0 / 3, 4.0 / 3, .75 })
                if (std::abs (result->bpm - named * ratio) <= .5) { ++octave; break; }
        auto* row = new juce::DynamicObject();
        row->setProperty ("file", entry.getFile().getRelativePathFrom (library));
        row->setProperty ("named", named);
        row->setProperty ("seconds", length);
        row->setProperty ("estimate", result ? juce::var (result->bpm) : juce::var());
        row->setProperty ("firstBeat", result ? juce::var (result->firstBeatSeconds) : juce::var());
        row->setProperty ("confidence", result ? juce::var (result->confidence) : juce::var());
        row->setProperty ("lengthGuess", guess);
        rows.add (juce::var (row));
        if (! hit) std::cout << "miss: " << name << " (" << juce::String (length, 1) << " s): " << (result ? juce::String (result->bpm, 2) : juce::String ("none")) << std::endl;
    }
    if (loops == 0) { std::cerr << "No loops with a tempo in their names under " << library.getFullPathName() << std::endl; return 1; }
    const auto share = [&] (int n) { return juce::String (100.0 * n / loops, 1) + "%"; };
    std::cout << loops << " loops: the estimate found " << share (found) << " within 0.5 BPM, the length guess " << share (byLength)
              << "; " << share (octave) << " a related tempo, " << share (none) << " none; " << juce::String (1000 * seconds / loops, 1) << " ms a loop" << std::endl;
    if (report != juce::File())
    {
        auto* summary = new juce::DynamicObject();
        summary->setProperty ("loops", loops);
        summary->setProperty ("found", found);
        summary->setProperty ("byLength", byLength);
        summary->setProperty ("related", octave);
        summary->setProperty ("none", none);
        summary->setProperty ("msPerLoop", 1000 * seconds / loops);
        summary->setProperty ("rows", rows);
        if (! report.replaceWithText (juce::JSON::toString (juce::var (summary)))) return 1;
    }
    return 0;
}
}
