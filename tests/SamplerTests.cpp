#include "NativeEffects.h"
#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

// The Sampler plays generated stereo sources. At whole-frame read positions its
// output must equal the source exactly; fractional positions follow closed-form
// sines. Engine renders, history and portable project collection are checked
// through the Session facade.
namespace halftrack
{
namespace
{
using Kind = Session::EffectKind;
namespace sp = instruments::sampler;
constexpr double pi = 3.14159265358979323846, rate = 48000;
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS sampler: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
float sourceLeft (int n) { return (float) (.5 * std::sin (n * .013) + .25 * std::sin (n * .171 + 1)); }
float sourceRight (int n) { return (float) (.4 * std::cos (n * .029) - .2 * std::sin (n * .093)); }
instruments::SampleData makeSample (int frames, double sampleRate = rate)
{
    instruments::SampleData s; s.sampleRate = sampleRate;
    for (int n = 0; n < frames; ++n) { s.left.push_back (sourceLeft (n)); s.right.push_back (sourceRight (n)); }
    return s;
}
Session::EffectSettings settings (std::initializer_list<std::pair<const char*, float>> overrides = {})
{
    // Unity level: no velocity scaling, instant attack and full sustain.
    auto s = defaultEffectSettings (Kind::sampler);
    s.values[sp::velocityAmp] = 0; s.values[sp::ampAttack] = 0; s.values[sp::ampSustain] = 1;
    for (const auto& [key, value] : overrides)
    {
        const auto index = effectParameterIndex (Kind::sampler, key);
        if (index < 0) throw std::runtime_error (std::string ("unknown sampler control ") + key);
        s.values[(size_t) index] = value;
    }
    return s;
}
struct Event { int at, note; float velocity; bool on = true; };
juce::AudioBuffer<float> play (const Session::EffectSettings& s, const instruments::SampleData* sample, std::vector<Event> events, int frames, int block = 0)
{
    instruments::InstrumentDSP dsp; dsp.prepare (rate, Kind::sampler); dsp.configure (s); dsp.setSample (sample);
    juce::AudioBuffer<float> out (2, frames); out.clear();
    int position = 0; size_t next = 0;
    while (position < frames)
    {
        while (next < events.size() && events[next].at <= position)
        {
            const auto& e = events[next++];
            if (e.on) dsp.noteOn (1, e.note, e.velocity); else dsp.noteOff (1, e.note);
        }
        auto end = frames;
        if (next < events.size()) end = std::min (end, events[next].at);
        if (block > 0) end = std::min (end, position + block - position % block);
        dsp.render (out.getWritePointer (0, position), out.getWritePointer (1, position), end - position);
        position = end;
    }
    return out;
}
// Largest difference from expected stereo values over [start, end).
double error (const juce::AudioBuffer<float>& a, const std::function<std::pair<double, double> (int)>& expected, int start, int end)
{
    double worst = 0;
    for (int n = start; n < end; ++n)
    {
        const auto [l, r] = expected (n);
        worst = std::max ({ worst, std::abs (a.getSample (0, n) - l), std::abs (a.getSample (1, n) - r) });
    }
    return worst;
}
double peak (const juce::AudioBuffer<float>& a, int start, int end)
{ return std::max (a.getMagnitude (0, start, end - start), a.getMagnitude (1, start, end - start)); }

void dspTests()
{
    const int frames = 4800;
    const auto sample = makeSample (frames);
    const auto exact = [&] (double scale, double offset = 0) { return [&, scale, offset] (int n) { const auto i = (int) (offset + n * scale); return std::pair<double, double> { sample.left[(size_t) i], sample.right[(size_t) i] }; }; };
    auto out = play (settings(), &sample, { { 0, 60, 1 } }, frames + 480);
    check (error (out, exact (1), 0, frames) == 0, "the root note plays the stereo source sample for sample");
    check (peak (out, frames, frames + 480) == 0, "playback stops at the end of the sample");
    out = play (settings(), &sample, { { 0, 72, 1 } }, frames);
    check (error (out, exact (2), 0, frames / 2) == 0 && peak (out, frames / 2, frames) == 0, "an octave above the root reads every second frame and ends at half the length");
    out = play (settings ({ { "transpose", 12 } }), &sample, { { 0, 60, 1 } }, frames / 2);
    check (error (out, exact (2), 0, frames / 2) == 0, "transpose shifts the read rate like the key");
    out = play (settings ({ { "rootNote", 67 } }), &sample, { { 0, 67, 1 } }, frames);
    check (error (out, exact (1), 0, frames) == 0, "the root note control sets the key that plays at the original pitch");
    const auto half = makeSample (frames, rate / 2);
    out = play (settings(), &half, { { 0, 60, 1 } }, 2 * frames);
    double even = 0; for (int n = 0; n < 2 * frames; n += 2) even = std::max ({ even, (double) std::abs (out.getSample (0, n) - half.left[(size_t) n / 2]) });
    check (even == 0, "a 24 kHz source plays at its own speed at 48 kHz");
    out = play (settings ({ { "start", .5f }, { "end", .75f } }), &sample, { { 0, 60, 1 } }, frames);
    check (error (out, exact (1, frames / 2), 0, frames / 4) == 0 && peak (out, frames / 4, frames) == 0, "start and end select the played region");
    out = play (settings ({ { "loop", 1 }, { "loopStart", .5f } }), &sample, { { 0, 60, 1 } }, 3 * frames);
    check (error (out, exact (1), 0, frames) == 0 && error (out, [&] (int n) { const auto i = frames / 2 + (n - frames) % (frames / 2); return std::pair<double, double> { sample.left[(size_t) i], sample.right[(size_t) i] }; }, frames, 3 * frames) == 0,
           "a looping clip repeats from the loop start to the end while held");
    // A 220 Hz sine transposed a fifth follows the closed-form 330 Hz sine through Hermite interpolation.
    instruments::SampleData sine; for (int n = 0; n < 48000; ++n) { const auto v = (float) (.5 * std::sin (2 * pi * 220 * n / rate)); sine.left.push_back (v); sine.right.push_back (v); }
    const auto ratio = std::exp2 (7 / 12.0);
    out = play (settings ({ { "transpose", 7 } }), &sine, { { 0, 60, 1 } }, 4800);
    check (error (out, [&] (int n) { const auto v = .5 * std::sin (2 * pi * 220 * n * ratio / rate); return std::pair<double, double> { v, v }; }, 2, 4800) < 2e-5,
           "fractional read positions match the transposed closed-form sine");
    out = play (settings ({ { "detune", 50 } }), &sine, { { 0, 60, 1 } }, 4800);
    check (error (out, [&] (int n) { const auto v = .5 * std::sin (2 * pi * 220 * n * std::exp2 (.5 / 12) / rate); return std::pair<double, double> { v, v }; }, 2, 4800) < 2e-5,
           "detune adds cents to the read rate");
    // Release: Classic fades after note-off; One-Shot Trigger ignores it; One-Shot Gate fades.
    const auto released = [&] (const Session::EffectSettings& s) { const auto o = play (s, &sample, { { 0, 60, 1 }, { 960, 60, 0, false } }, frames); return peak (o, 960 + 2400, frames); };
    check (released (settings ({ { "ampRelease", 10 } })) < 1e-4, "Classic mode releases after note-off");
    check (std::abs (released (settings ({ { "mode", 1 } })) - peak (out = play (settings(), &sample, { { 0, 60, 1 } }, frames), 960 + 2400, frames)) < 1e-9,
           "a triggered One-Shot plays to its end after note-off");
    check (released (settings ({ { "mode", 1 }, { "trigger", 1 }, { "ampRelease", 10 } })) < 1e-4, "a gated One-Shot fades out after note-off");
    out = play (settings ({ { "velocityAmp", 1 } }), &sample, { { 0, 60, .5f } }, frames);
    check (error (out, [&] (int n) { return std::pair<double, double> { .5 * sample.left[(size_t) n], .5 * sample.right[(size_t) n] }; }, 0, frames) < 1e-7,
           "full velocity sensitivity scales the level linearly");
    const auto gain = std::pow (10.0, -6 / 20.0);
    out = play (settings ({ { "volumeDb", -6 }, { "pan", -1 } }), &sample, { { 0, 60, 1 } }, frames);
    check (error (out, [&] (int n) { return std::pair<double, double> { gain * sample.left[(size_t) n], 0.0 }; }, 0, frames) < 1e-7,
           "volume scales by its dB factor and hard-left pan silences the right channel");
    out = play (settings(), &sample, { { 0, 60, 1 }, { 0, 72, 1 } }, frames / 2);
    check (error (out, [&] (int n) { return std::pair<double, double> { sample.left[(size_t) n] + sample.left[(size_t) 2 * n], sample.right[(size_t) n] + sample.right[(size_t) 2 * n] }; }, 0, frames / 2) < 1e-6,
           "polyphonic notes sum");
    out = play (settings ({ { "voices", 0 } }), &sample, { { 0, 60, 1 }, { 480, 72, 1 } }, frames / 2);
    check (error (out, [&] (int n) { const auto i = n < 480 ? n : 2 * (n - 480); return std::pair<double, double> { sample.left[(size_t) i], sample.right[(size_t) i] }; }, 0, frames / 2) == 0,
           "Mono restarts the single voice at the new key");
    const auto whole = play (settings ({ { "filterType", 1 }, { "cutoffHz", 900 }, { "loop", 1 } }), &sample, { { 0, 60, 1 }, { 3000, 64, .7f }, { 7000, 60, 0, false } }, 12000);
    const auto blocks = play (settings ({ { "filterType", 1 }, { "cutoffHz", 900 }, { "loop", 1 } }), &sample, { { 0, 60, 1 }, { 3000, 64, .7f }, { 7000, 60, 0, false } }, 12000, 37);
    check (error (blocks, [&] (int n) { return std::pair<double, double> { whole.getSample (0, n), whole.getSample (1, n) }; }, 0, 12000) == 0, "render is independent of block size");
    instruments::InstrumentDSP dsp; dsp.prepare (rate, Kind::sampler); dsp.configure (settings ({ { "loop", 1 } })); dsp.setSample (&sample);
    dsp.noteOn (1, 60, 1); std::vector<float> l (64), r (64); dsp.render (l.data(), r.data(), 64);
    const auto other = makeSample (100);
    dsp.setSample (&other); std::fill (l.begin(), l.end(), 0.0f); dsp.render (l.data(), r.data(), 64);
    check (dsp.activeVoices() == 0 && *std::max_element (l.begin(), l.end()) == 0, "changing the sample silences voices that played the old one");
    dsp.setSample (nullptr); dsp.noteOn (1, 60, 1); dsp.render (l.data(), r.data(), 64);
    check (dsp.activeVoices() == 0, "without a sample the instrument stays silent");
}

void write (const juce::File& f, const instruments::SampleData& s)
{
    juce::AudioBuffer<float> a (2, s.frames());
    for (int n = 0; n < s.frames(); ++n) { a.setSample (0, n, s.left[(size_t) n]); a.setSample (1, n, s.right[(size_t) n]); }
    juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (f.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (s.sampleRate).withNumChannels (2).withBitsPerSample (32));
    check (writer && writer->writeFromAudioSampleBuffer (a, 0, s.frames()), "write generated sample " + f.getFileName());
}
juce::AudioBuffer<float> read (const juce::File& f)
{
    juce::AudioFormatManager formats; formats.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
    check (r != nullptr, "read render"); juce::AudioBuffer<float> x (2, (int) r->lengthInSamples); r->read (&x, 0, x.getNumSamples(), 0, true, true); return x;
}
double difference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumSamples() != b.getNumSamples()) return 1e9;
    double worst = 0; for (int c = 0; c < 2; ++c) for (int n = 0; n < a.getNumSamples(); ++n) worst = std::max (worst, (double) std::abs (a.getSample (c, n) - b.getSample (c, n)));
    return worst;
}
Session::PluginInfo instrument (Session& s) { const auto snapshot = s.snapshot(); return snapshot.tracks.back().instrument; }

void engineTests (const juce::File& root)
{
    const auto source = makeSample (12000), second = makeSample (6000, 24000);
    const auto first = root.getChildFile ("Sources").getChildFile ("hit.wav"), alternate = root.getChildFile ("Sources").getChildFile ("alternate.wav");
    ok (first.getParentDirectory().createDirectory(), "create source folder");
    write (first, source); write (alternate, second);
    const auto text = root.getChildFile ("notes.txt"); check (text.replaceWithText ("not audio"), "write a non-audio file");
    Session s (false);
    ok (s.addMidiTrack ("Sampler"), "add MIDI track");
    const auto trackId = s.snapshot().tracks.back().id;
    ok (s.addMidiClip (trackId, 0, 4), "add clip");
    { const auto snapshot = s.snapshot(); ok (s.addMidiNote (snapshot.tracks.back().clips[0].id, 60, 0, 1, 1), "add C3 at full velocity"); }
    const auto builtIn = root.getChildFile ("built-in.wav"), render = root.getChildFile ("render.wav");
    ok (s.exportWav (builtIn, 48000, 32, .5), "render the built-in synth");
    check (s.setInstrumentSample (s.snapshot().tracks[0].id, first).failed(), "audio tracks have no Sampler");
    check (s.setInstrumentSample (trackId, text).failed() && s.setInstrumentSample (trackId, root.getChildFile ("missing.wav")).failed()
           && ! instrument (s).isNativeEffect, "unreadable and missing samples are rejected without changing the instrument");
    ok (s.setInstrumentSample (trackId, first), "load a sample onto the MIDI track");
    auto info = instrument (s);
    check (info.isNativeEffect && info.effect.kind == Kind::sampler && info.sample == first, "loading a sample switches the instrument to a Sampler that reports it");
    ok (s.setNativeEffectSettings (info.id, settings()), "configure unity playback");
    ok (s.exportWav (render, 48000, 32, .5), "render the Sampler");
    const auto rendered = read (render);
    check (error (rendered, [&] (int n) { return std::pair<double, double> { source.left[(size_t) n], source.right[(size_t) n] }; }, 0, 12000) < 1e-6
           && peak (rendered, 12000, rendered.getNumSamples()) < 1e-6, "the engine render equals the source sample for sample, then silence");
    s.undo(); s.undo(); ok (s.exportWav (render, 48000, 32, .5), "render after undo");
    check (! instrument (s).isNativeEffect && difference (read (render), read (builtIn)) == 0, "one undo step removes the Sampler and its sample together");
    s.redo(); s.redo(); ok (s.exportWav (render, 48000, 32, .5), "render after redo");
    check (instrument (s).sample == first && difference (read (render), rendered) == 0, "redo restores the Sampler, sample and settings");
    ok (s.setInstrumentSample (trackId, alternate), "load another sample into the Sampler");
    check (instrument (s).sample == alternate && instrument (s).id == info.id && instrument (s).effect == settings(), "a new sample keeps the instance and its settings");
    ok (s.exportWav (render, 48000, 32, .5), "render the alternate sample");
    double even = 0; const auto alt = read (render); for (int n = 0; n < 12000; n += 2) even = std::max ({ even, (double) std::abs (alt.getSample (0, n) - second.left[(size_t) n / 2]) });
    check (even < 1e-6, "a 24 kHz sample plays at its own speed through the engine");
    s.undo(); check (instrument (s).sample == first, "undo restores the previous sample"); s.redo();
    ok (s.setInstrumentSample (trackId, first), "return to the first sample");
    const auto project = root.getChildFile ("Project").getChildFile ("Sampler.halftrack");
    ok (project.getParentDirectory().createDirectory(), "create project folder");
    ok (s.saveProject (project), "save the Sampler project");
    const auto assets = project.getSiblingFile ("Sampler Assets");
    info = instrument (s);
    check (info.sample.isAChildOf (assets) && info.sample.existsAsFile() && first.existsAsFile(), "saving collects the sample into the project assets and uses the copy");
    const auto xml = juce::XmlDocument::parse (project);
    juce::String saved;
    std::function<void (const juce::XmlElement&)> find = [&] (const juce::XmlElement& e) { if (e.hasAttribute (samplerSampleProperty)) saved = e.getStringAttribute (samplerSampleProperty); for (auto* c : e.getChildIterator()) find (*c); };
    check (xml != nullptr, "parse the saved project"); find (*xml);
    check (saved.isNotEmpty() && ! juce::File::isAbsolutePath (saved) && project.getParentDirectory().getChildFile (saved) == info.sample, "the project stores the sample relative to itself");
    const auto moved = root.getChildFile ("Moved");
    check (project.getParentDirectory().copyDirectoryTo (moved), "copy the project folder elsewhere");
    ok (s.loadProject (moved.getChildFile ("Sampler.halftrack")), "open the moved project");
    info = instrument (s);
    check (info.sample.isAChildOf (moved) && info.effect == settings(), "the moved project resolves its own collected sample");
    ok (s.exportWav (render, 48000, 32, .5), "render the moved project");
    check (difference (read (render), rendered) == 0, "the reopened Sampler renders identically");
    juce::String copy; ok (s.duplicateTrack (s.snapshot().tracks.back().id, copy), "duplicate the Sampler track");
    check (s.snapshot().tracks.back().instrument.sample == info.sample, "duplicated tracks keep the sample");
    check (info.sample.deleteFile(), "remove the collected sample");
    const auto tracks = s.snapshot().tracks.size();
    const auto missing = s.loadProject (moved.getChildFile ("Sampler.halftrack"));
    check (missing.failed() && missing.getErrorMessage().startsWith ("Missing sample") && s.snapshot().tracks.size() == tracks && s.snapshot().tracks.back().instrument.sample == info.sample,
           "a missing sample rejects loading and keeps the open project");
}
}
int runSamplerTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("sampler-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        dspTests();
        engineTests (root);
        std::cout << "Sampler passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL sampler: " << e.what() << std::endl; return 1; }
}
}
