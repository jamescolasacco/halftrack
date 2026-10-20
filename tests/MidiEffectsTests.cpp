#include "NativeEffects.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

// MIDI effect contract tests. Processor checks compare complete generated event
// lists with hand-derived expectations; engine checks render an instrument
// through each device and compare every sample with explicitly written notes.
namespace halftrack
{
namespace
{
using Kind = Session::EffectKind;
using Settings = Session::EffectSettings;
using midifx::Event;
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS MIDI effects: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
Settings with (Kind kind, std::initializer_list<std::pair<const char*, float>> values, float bpm = 120)
{
    auto s = defaultEffectSettings (kind); s.tempoBpm = bpm;
    for (const auto& [key, value] : values)
    {
        const auto index = effectParameterIndex (kind, key);
        if (index < 0) throw std::runtime_error (std::string ("unknown test parameter ") + key);
        s.values[(size_t) index] = value;
    }
    if (! validEffectSettings (s)) throw std::runtime_error ("invalid test settings");
    return s;
}
// Absolute-time event used for expectations: positive velocity = note on.
struct Note { long long time; int pitch, velocity; bool operator== (const Note&) const = default; };
struct Input { long long time; int pitch, velocity; };
std::vector<Note> run (const Settings& s, const std::vector<Input>& input, long long length, int block = 512, bool timeline = true)
{
    midifx::Processor p; p.prepare (48000); p.configure (s);
    std::vector<Note> out; size_t next = 0;
    std::array<Event, 2048> generated {};
    for (long long start = 0; start < length; start += block)
    {
        std::vector<Event> in;
        while (next < input.size() && input[next].time < start + block)
        {
            const auto& e = input[next++]; const auto offset = (int) (e.time - start);
            in.push_back (e.velocity > 0 ? Event::noteOn (offset, 0, e.pitch, e.velocity, 0) : Event::noteOff (offset, 0, e.pitch, 0));
        }
        const auto samples = (int) std::min<long long> (block, length - start);
        const auto count = p.process (in.data(), (int) in.size(), samples, timeline ? start / 48000.0 : -1.0, generated.data(), (int) generated.size());
        for (int i = 0; i < count; ++i)
        {
            const auto& e = generated[(size_t) i];
            check (e.sample >= 0 && e.sample < samples, "generated events stay inside their block");
            out.push_back ({ start + e.sample, e.data1, e.isNoteOn() ? e.data2 : 0 });
        }
    }
    for (size_t i = 1; i < out.size(); ++i) if (out[i].time < out[i - 1].time) throw std::runtime_error ("generated events out of order");
    return out;
}
juce::String describe (const std::vector<Note>& notes)
{
    juce::String s; for (const auto& n : notes) s << n.time << ":" << n.pitch << "/" << n.velocity << " ";
    return s;
}
void expect (const std::vector<Note>& actual, const std::vector<Note>& expected, const juce::String& text)
{
    if (actual != expected) std::cout << "  actual   " << describe (actual) << "\n  expected " << describe (expected) << std::endl;
    check (actual == expected, text);
}

void mappingTests()
{
    expect (run (with (Kind::pitch, { { "semitones", 12 } }), { { 10, 60, 90 }, { 400, 60, 0 } }, 1024),
            { { 10, 72, 90 }, { 400, 72, 0 } }, "pitch transposes note-on and its matching release");
    expect (run (with (Kind::pitch, { { "lowest", 50 }, { "highest", 70 } }), { { 0, 40, 90 }, { 1, 60, 80 }, { 2, 80, 70 }, { 600, 40, 0 }, { 601, 60, 0 }, { 602, 80, 0 } }, 1024),
            { { 1, 60, 80 }, { 601, 60, 0 } }, "pitch range blocks notes and their releases outside the input span");
    expect (run (with (Kind::pitch, { { "semitones", 48 } }), { { 0, 100, 90 }, { 10, 100, 0 } }, 512), {}, "out-of-range transposition is blocked");
    {
        midifx::Processor p; p.prepare (48000); p.configure (with (Kind::pitch, { { "semitones", 5 } }));
        std::array<Event, 16> out {};
        const auto on = Event::noteOn (0, 3, 60, 100, 7); check (p.process (&on, 1, 64, 0, out.data(), 16) == 1 && out[0].data1 == 65 && out[0].channel() == 3 && out[0].source == 7, "channel and source identity are preserved");
        p.configure (with (Kind::pitch, { { "semitones", -5 } }));
        const auto off = Event::noteOff (0, 3, 60, 7); check (p.process (&off, 1, 64, 0, out.data(), 16) == 1 && out[0].data1 == 65 && out[0].isNoteOff(), "a release uses the pitch chosen at its note-on after a parameter change");
    }
    expect (run (with (Kind::chord, {}), { { 0, 60, 100 }, { 300, 60, 0 } }, 512),
            { { 0, 60, 100 }, { 0, 64, 100 }, { 0, 67, 100 }, { 300, 60, 0 }, { 300, 64, 0 }, { 300, 67, 0 } }, "default chord adds a major third and fifth");
    expect (run (with (Kind::chord, { { "shift1", 12 }, { "shift2", -12 }, { "shift3", 12 }, { "velocity1", .5f }, { "velocity2", 0 } }), { { 0, 60, 100 }, { 300, 60, 0 } }, 512),
            { { 0, 60, 100 }, { 0, 72, 50 }, { 0, 48, 1 }, { 300, 60, 0 }, { 300, 72, 0 }, { 300, 48, 0 } }, "duplicate shifts merge and per-shift velocity scales with a floor of 1");
    expect (run (with (Kind::chord, {}), { { 0, 60, 100 }, { 10, 64, 90 }, { 100, 60, 0 }, { 200, 64, 0 } }, 512),
            { { 0, 60, 100 }, { 0, 64, 100 }, { 0, 67, 100 }, { 10, 64, 0 }, { 10, 64, 90 }, { 10, 68, 90 }, { 10, 71, 90 },
              { 100, 60, 0 }, { 100, 67, 0 }, { 200, 64, 0 }, { 200, 68, 0 }, { 200, 71, 0 } },
            "a shared chord tone retriggers and releases only after its last owner");

    int errors = 0;
    for (int root = 0; root < 12; ++root) for (size_t scale = 0; scale < midifx::scaleMasks.size(); ++scale) for (int pitch = 0; pitch < 128; ++pitch)
    {
        int best = pitch, distance = 1000;
        for (int candidate = -12; candidate < 140; ++candidate)
            if ((midifx::scaleMasks[scale] >> (((candidate - root) % 12 + 12) % 12)) & 1)
                if (const auto d = std::abs (candidate - pitch); d < distance) { distance = d; best = candidate; }
        if (midifx::quantizeToScale (pitch, root, midifx::scaleMasks[scale]) != best) ++errors;
    }
    check (errors == 0, "scale quantization matches a brute-force nearest-degree search with downward ties for every root, scale and note");
    expect (run (with (Kind::scale, { { "root", 2 }, { "scaleType", 1 }, { "transpose", 12 } }), { { 0, 63, 100 }, { 5, 63, 0 } }, 64),
            { { 0, 74, 100 }, { 5, 74, 0 } }, "D minor maps D# to D before transposition");

    expect (run (with (Kind::velocity, { { "mode", 2 }, { "fixed", 99 } }), { { 0, 60, 3 }, { 1, 62, 127 } }, 64), { { 0, 60, 99 }, { 1, 62, 99 } }, "fixed mode sets one output velocity");
    expect (run (with (Kind::velocity, { { "outLow", 40 }, { "outHigh", 80 } }), { { 0, 60, 1 }, { 1, 61, 64 }, { 2, 62, 127 } }, 64),
            { { 0, 60, 40 }, { 1, 61, 60 }, { 2, 62, 80 } }, "linear clip mode maps the input span to the output span");
    expect (run (with (Kind::velocity, { { "mode", 1 }, { "inLow", 50 }, { "inHigh", 90 }, { "outLow", 50 }, { "outHigh", 90 } }),
                 { { 0, 60, 49 }, { 1, 61, 50 }, { 2, 62, 91 }, { 3, 63, 77 }, { 4, 60, 0 }, { 5, 61, 0 } }, 64),
            { { 1, 61, 50 }, { 3, 63, 77 }, { 5, 61, 0 } }, "gate mode drops notes outside the input span together with their releases");
    const auto soft = run (with (Kind::velocity, { { "drive", .5f } }), { { 0, 60, 64 } }, 64), hard = run (with (Kind::velocity, { { "drive", -.5f } }), { { 0, 60, 64 } }, 64);
    check (soft[0].velocity > 64 && hard[0].velocity < 64 && soft[0].velocity == (int) std::lround (1 + std::pow (63.0 / 126, .5) * 126), "drive bends the curve in both directions with the stated exponent");

    expect (run (with (Kind::random, { { "chance", 0 } }), { { 0, 60, 100 }, { 1, 60, 0 } }, 64), { { 0, 60, 100 }, { 1, 60, 0 } }, "zero chance passes notes through");
    expect (run (with (Kind::random, { { "chance", 1 }, { "interval", 7 } }), { { 0, 60, 100 }, { 1, 60, 0 } }, 64), { { 0, 67, 100 }, { 1, 67, 0 } }, "certain change with one choice adds one interval");
    expect (run (with (Kind::random, { { "chance", 1 }, { "choices", 3 }, { "interval", 2 }, { "sign", 1 }, { "mode", 1 } }),
                 { { 0, 60, 100 }, { 1, 60, 0 }, { 2, 60, 100 }, { 3, 60, 0 }, { 4, 60, 100 }, { 5, 60, 0 }, { 6, 60, 100 }, { 7, 60, 0 } }, 64),
            { { 0, 58, 100 }, { 1, 58, 0 }, { 2, 56, 100 }, { 3, 56, 0 }, { 4, 54, 100 }, { 5, 54, 0 }, { 6, 58, 100 }, { 7, 58, 0 } }, "alternate mode cycles downward choices");
    std::vector<Input> many; for (int i = 0; i < 400; ++i) { many.push_back ({ i * 2, 60, 100 }); many.push_back ({ i * 2 + 1, 60, 0 }); }
    const auto first = run (with (Kind::random, { { "chance", .5f }, { "choices", 4 }, { "sign", 2 } }), many, 1024), second = run (with (Kind::random, { { "chance", .5f }, { "choices", 4 }, { "sign", 2 } }), many, 1024);
    int changed = 0, up = 0, down = 0; bool bounded = true;
    for (const auto& n : first) if (n.velocity > 0) { changed += n.pitch != 60; up += n.pitch > 60; down += n.pitch < 60; bounded = bounded && (n.pitch == 60 || std::abs (n.pitch - 60) % 12 == 0) && std::abs (n.pitch - 60) <= 48; }
    check (first == second && changed > 120 && changed < 280 && up > 30 && down > 30 && bounded, "seeded random changes about half the notes in both directions by whole intervals, reproducibly");
}

void lengthTests()
{
    expect (run (with (Kind::noteLength, { { "timing", 0 }, { "lengthMs", 10 } }), { { 10, 60, 100 }, { 50, 60, 0 } }, 1024, 128),
            { { 10, 60, 100 }, { 490, 60, 0 } }, "fixed time replaces the incoming release across block boundaries");
    expect (run (with (Kind::noteLength, { { "timing", 0 }, { "lengthMs", 10 }, { "trigger", 1 } }), { { 10, 60, 90 }, { 50, 60, 0 } }, 1024, 128),
            { { 50, 60, 90 }, { 530, 60, 0 } }, "release trigger starts the note at key-up with its original velocity");
    expect (run (with (Kind::noteLength, { { "division", 4 }, { "gate", .5f } }, 120), { { 0, 60, 100 } }, 12000),
            { { 0, 60, 100 }, { 6000, 60, 0 } }, "tempo division and gate scale the generated length");
    expect (run (with (Kind::noteLength, { { "timing", 0 }, { "lengthMs", 10 } }), { { 0, 60, 100 }, { 100, 60, 100 } }, 1024),
            { { 0, 60, 100 }, { 100, 60, 0 }, { 100, 60, 100 }, { 580, 60, 0 } }, "a repeated pitch ends the previous generated note first");
}

void arpeggiatorTests()
{
    const std::vector<Input> chord { { 0, 67, 90 }, { 0, 60, 100 }, { 0, 64, 80 }, { 48000, 60, 0 }, { 48000, 64, 0 }, { 48000, 67, 0 } };
    const auto steps = [] (std::initializer_list<int> pitches, std::initializer_list<int> velocities, long long step = 6000, long long gate = 3000)
    {
        std::vector<Note> notes; long long t = 0; auto v = velocities.begin();
        for (int p : pitches) { notes.push_back ({ t, p, *v }); notes.push_back ({ t + gate, p, 0 }); t += step; if (v + 1 != velocities.end()) ++v; }
        std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.time < b.time; });
        return notes;
    };
    expect (run (with (Kind::arpeggiator, {}), chord, 60000), steps ({ 60, 64, 67, 60, 64, 67, 60, 64 }, { 100, 80, 90, 100, 80, 90, 100, 80 }), "up style plays sixteenths on the transport grid");
    expect (run (with (Kind::arpeggiator, { { "style", 1 } }), chord, 60000), steps ({ 67, 64, 60, 67, 64, 60, 67, 64 }, { 90, 80, 100, 90, 80, 100, 90, 80 }), "down style");
    expect (run (with (Kind::arpeggiator, { { "style", 2 } }), chord, 60000), steps ({ 60, 64, 67, 64, 60, 64, 67, 64 }, { 100, 80, 90, 80, 100, 80, 90, 80 }), "up-down style shares its turning notes");
    expect (run (with (Kind::arpeggiator, { { "style", 6 } }), chord, 60000), steps ({ 67, 60, 64, 67, 60, 64, 67, 60 }, { 90, 100, 80, 90, 100, 80, 90, 100 }), "as-played style follows arrival order");
    const std::vector<Input> four { { 0, 71, 100 }, { 0, 60, 100 }, { 0, 67, 100 }, { 0, 64, 100 }, { 24000, 60, 0 }, { 24000, 64, 0 }, { 24000, 67, 0 }, { 24000, 71, 0 } };
    expect (run (with (Kind::arpeggiator, { { "style", 4 } }), four, 30000), steps ({ 60, 71, 64, 67 }, { 100 }), "converge alternates the outer notes inward");
    expect (run (with (Kind::arpeggiator, { { "style", 5 } }), four, 30000), steps ({ 67, 64, 71, 60 }, { 100 }), "diverge moves from the middle outward");
    expect (run (with (Kind::arpeggiator, { { "octaves", 1 }, { "velocityMode", 1 }, { "fixedVelocity", 64 }, { "rate", 4 }, { "gate", 1 } }), chord, 60000),
            steps ({ 60, 64, 67, 72 }, { 64 }, 12000, 12000), "two octaves, fixed velocity, eighth notes and legato gate");
    expect (run (with (Kind::arpeggiator, { { "gate", 2 }, { "rate", 4 } }), { { 0, 60, 100 }, { 0, 64, 100 }, { 36000, 60, 0 }, { 36000, 64, 0 } }, 60000),
            { { 0, 60, 100 }, { 12000, 64, 100 }, { 24000, 60, 0 }, { 24000, 60, 100 }, { 36000, 64, 0 }, { 48000, 60, 0 } }, "gates longer than a step overlap and retrigger a repeated pitch");
    expect (run (with (Kind::arpeggiator, {}), { { 1000, 60, 100 }, { 20000, 60, 0 } }, 30000),
            { { 6000, 60, 100 }, { 9000, 60, 0 }, { 12000, 60, 100 }, { 15000, 60, 0 }, { 18000, 60, 100 }, { 21000, 60, 0 } }, "a key pressed between grid points waits for the next step");
    expect (run (with (Kind::arpeggiator, {}), { { 1000, 60, 100 }, { 14000, 60, 0 } }, 30000, 512, false),
            { { 1000, 60, 100 }, { 4000, 60, 0 }, { 7000, 60, 100 }, { 10000, 60, 0 }, { 13000, 60, 100 }, { 16000, 60, 0 } }, "stopped transport anchors a free-running grid at the first key");
    expect (run (with (Kind::arpeggiator, { { "hold", 1 } }), { { 0, 60, 100 }, { 100, 60, 0 }, { 13000, 64, 100 }, { 13100, 64, 0 } }, 30000),
            { { 0, 60, 100 }, { 3000, 60, 0 }, { 6000, 60, 100 }, { 9000, 60, 0 }, { 12000, 60, 100 }, { 15000, 60, 0 }, { 18000, 64, 100 }, { 21000, 64, 0 }, { 24000, 64, 100 }, { 27000, 64, 0 } },
            "hold latches released keys until a new gesture replaces them");
    const auto blocks = [&] (int size) { return run (with (Kind::arpeggiator, { { "style", 7 }, { "octaves", 2 } }), chord, 60000, size); };
    check (blocks (1) == blocks (64) && blocks (64) == blocks (509) && blocks (2048).size() == 16, "random style is deterministic and independent of block size");
    midifx::Processor p; p.prepare (48000); p.configure (with (Kind::chord, {}));
    std::array<Event, 32> out {};
    const auto on = Event::noteOn (0, 0, 60, 100, 0); p.process (&on, 1, 64, 0, out.data(), 32);
    check (p.allNotesOff (5, out.data(), 32) == 3 && out[0].isNoteOff() && out[0].sample == 5, "all-notes-off releases every generated output");
}

void writeNotes (Session& s, const juce::String& trackId, const std::vector<Session::MidiNoteInfo>& notes)
{
    ok (s.addMidiClip (trackId, 0, 4), "add MIDI clip");
    const auto snapshot = s.snapshot();
    juce::StringArray ids; ok (s.addMidiNotes (snapshot.tracks.back().clips.back().id, notes, ids), "write explicit notes");
}
juce::AudioBuffer<float> render (const juce::File& file, const std::vector<Session::MidiNoteInfo>& notes, std::optional<Settings> effect)
{
    Session s (false); ok (s.addMidiTrack ("MIDI effect"), "add instrument track");
    const auto track = s.snapshot().tracks.back(); writeNotes (s, track.id, notes);
    if (effect)
    {
        ok (s.addNativeEffect ((int) s.snapshot().tracks.size() - 1, effect->kind), "insert MIDI effect before the instrument");
        const auto devices = s.snapshot().tracks.back().plugins;
        check (! devices.empty() && devices.front().effect.kind == effect->kind, "MIDI effect is first in the device chain");
        auto settings = *effect; settings.tempoBpm = devices.front().effect.tempoBpm;
        ok (s.setNativeEffectSettings (devices.front().id, settings), "configure MIDI effect");
    }
    ok (s.exportWav (file, 48000, 24, .5), "render instrument");
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    check (reader != nullptr, "read rendered instrument");
    juce::AudioBuffer<float> audio (2, (int) reader->lengthInSamples); reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);
    return audio;
}
double difference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumSamples() != b.getNumSamples()) return 1e9;
    double worst = 0;
    for (int c = 0; c < 2; ++c) for (int n = 0; n < a.getNumSamples(); ++n) worst = std::max (worst, (double) std::abs (a.getSample (c, n) - b.getSample (c, n)));
    return worst;
}
Session::MidiNoteInfo note (int pitch, double start, double length, float velocity = .8f) { Session::MidiNoteInfo n; n.pitch = pitch; n.startBeat = start; n.lengthBeats = length; n.velocity = velocity; return n; }

void engineTests (const juce::File& root)
{
    const auto a = root.getChildFile ("effect.wav"), b = root.getChildFile ("explicit.wav");
    const auto same = [&] (const std::vector<Session::MidiNoteInfo>& input, const Settings& s, const std::vector<Session::MidiNoteInfo>& expected, const juce::String& text)
    {
        const auto processed = render (a, input, s), reference = render (b, expected, std::nullopt);
        check (reference.getMagnitude (0, reference.getNumSamples()) > .01f, "reference render is audible");
        check (difference (processed, reference) < 2e-6, text);
    };
    same ({ note (60, 0, 1) }, with (Kind::pitch, { { "semitones", 7 } }), { note (67, 0, 1) }, "rendered pitch effect equals the transposed note");
    same ({ note (60, 0, 1) }, with (Kind::chord, {}), { note (60, 0, 1), note (64, 0, 1), note (67, 0, 1) }, "rendered chord equals three written notes");
    same ({ note (61, 0, 1) }, with (Kind::scale, {}), { note (60, 0, 1) }, "rendered scale effect equals the quantized note");
    same ({ note (60, 0, 1, .3f) }, with (Kind::velocity, { { "mode", 2 }, { "fixed", 100 } }), { note (60, 0, 1, 100.0f / 127) }, "rendered fixed velocity equals the written velocity");
    same ({ note (60, 0, 2) }, with (Kind::noteLength, { { "division", 4 } }), { note (60, 0, .5) }, "rendered note length equals a written eighth note");
    same ({ note (60, 0, 1), note (64, 0, 1), note (67, 0, 1) }, with (Kind::arpeggiator, {}),
          { note (60, 0, .125), note (64, .25, .125), note (67, .5, .125), note (60, .75, .125) }, "rendered arpeggio equals written sixteenth notes");
    same ({ note (60, 0, 1) }, with (Kind::random, { { "chance", 0 } }), { note (60, 0, 1) }, "rendered zero-chance random is transparent");

    Session s (false); ok (s.addMidiTrack ("Chain"), "add chain track"); const int track = (int) s.snapshot().tracks.size() - 1;
    writeNotes (s, s.snapshot().tracks.back().id, { note (61, 0, 1) });
    check (s.addNativeEffect (0, Kind::pitch).failed() && s.snapshot().tracks[0].plugins.empty(), "audio tracks reject MIDI effects without an edit");
    ok (s.addNativeEffect (track, Kind::pitch), "insert pitch"); ok (s.addNativeEffect (track, Kind::scale), "insert scale after pitch");
    ok (s.addNativeEffect (track, Kind::saturator), "insert an audio effect after the instrument");
    auto devices = s.snapshot().tracks.back().plugins;
    check (devices.size() == 3 && devices[0].effect.kind == Kind::pitch && devices[1].effect.kind == Kind::scale && devices[2].effect.kind == Kind::saturator, "MIDI effects stay in order ahead of audio devices");
    ok (s.setNativeEffectSettings (devices[0].id, with (Kind::pitch, { { "semitones", 1 } })), "pitch up by a semitone");
    ok (s.setNativeEffectSettings (devices[2].id, with (Kind::saturator, { { "mix", 0 } })), "keep the audio device's path exactly dry");
    const auto chain = root.getChildFile ("chain.wav"), swapped = root.getChildFile ("swapped.wav");
    ok (s.exportWav (chain, 48000, 24, .5), "render pitch then scale");
    check (s.movePlugin (track, 0, 2).failed() && s.movePlugin (track, 2, 0).failed(), "MIDI and audio devices cannot cross the instrument");
    ok (s.movePlugin (track, 0, 1), "reorder MIDI effects"); ok (s.exportWav (swapped, 48000, 24, .5), "render scale then pitch");
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    const auto read = [&] (const juce::File& f) { std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f)); juce::AudioBuffer<float> x (2, (int) r->lengthInSamples); r->read (&x, 0, x.getNumSamples(), 0, true, true); return x; };
    // 61 + 1 = 62 (D, in C major) versus quantize(61) = 60 then + 1 = 61.
    check (difference (read (chain), render (b, { note (62, 0, 1) }, std::nullopt)) < 2e-6 && difference (read (swapped), render (b, { note (61, 0, 1) }, std::nullopt)) < 2e-6,
           "MIDI effect order changes the rendered result as specified");
    s.undo(); ok (s.exportWav (swapped, 48000, 24, .5), "render after undoing reorder"); check (difference (read (swapped), read (chain)) == 0, "undo restores MIDI effect order");
    ok (s.setPluginEnabled (track, 0, false), "bypass pitch"); ok (s.setPluginEnabled (track, 1, false), "bypass scale");
    ok (s.exportWav (swapped, 48000, 24, .5), "render bypassed MIDI effects");
    check (difference (read (swapped), render (b, { note (61, 0, 1) }, std::nullopt)) < 2e-6, "bypassed MIDI effects pass notes through unchanged");
    s.undo(); s.undo();
    const auto project = root.getChildFile ("midi-effects.halftrack");
    ok (s.saveProject (project), "save MIDI effect chain"); ok (s.loadProject (project), "reopen MIDI effect chain");
    devices = s.snapshot().tracks.back().plugins;
    check (devices.size() == 3 && devices[0].effect == with (Kind::pitch, { { "semitones", 1 } }, devices[0].effect.tempoBpm) && devices[1].effect.kind == Kind::scale, "saved MIDI effects reopen with order and settings");
    ok (s.exportWav (swapped, 48000, 24, .5), "render reopened chain"); check (difference (read (swapped), read (chain)) == 0, "reopened chain renders identically");
    auto fractional = devices[0].effect; fractional.values[0] = 1.5f;
    check (s.setNativeEffectSettings (devices[0].id, fractional).failed(), "integer MIDI controls reject fractional values");
    ok (s.removePlugin (track, 0), "remove pitch"); s.undo(); check (s.snapshot().tracks.back().plugins.size() == 3, "undo restores a removed MIDI effect");
}
}
int runMidiEffectsTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("midi-effects-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        mappingTests(); lengthTests(); arpeggiatorTests(); engineTests (root);
        std::cout << "MIDI effects passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL MIDI effects: " << e.what() << std::endl; return 1; }
}
}
