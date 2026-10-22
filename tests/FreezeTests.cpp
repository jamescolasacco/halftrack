#include "NativeEffects.h"
#include "Session.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Freeze and Flatten. A Sampler plays a generated stereo source from a MIDI clip;
// frozen and flattened renders are compared with the live render, and device and
// clip edits must be locked while the track is frozen. Renders use the 44.1 kHz
// rate the test engine freezes at, so no comparison includes resampling. A Sends Only
// audio track freezes and plays its freeze only through its send or to a track taking its
// audio, through the synthetic device, since exports render frozen tracks from their clips.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
juce::AudioBuffer<float> captureSyntheticTestDevice (Session&, int blocks);
namespace
{
constexpr double rate = 44100;
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS freeze: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
juce::AudioBuffer<float> read (const juce::File& f)
{
    juce::AudioFormatManager formats; formats.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
    check (r != nullptr, "read render"); juce::AudioBuffer<float> x (2, (int) r->lengthInSamples); r->read (&x, 0, x.getNumSamples(), 0, true, true); return x;
}
double difference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, float scale = 1)
{
    double worst = 0; const int n = std::min (a.getNumSamples(), b.getNumSamples());
    for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) worst = std::max (worst, (double) std::abs (a.getSample (c, i) - scale * b.getSample (c, i)));
    for (int c = 0; c < 2; ++c) for (int i = n; i < std::max (a.getNumSamples(), b.getNumSamples()); ++i)
        worst = std::max (worst, (double) std::abs (i < a.getNumSamples() ? a.getSample (c, i) : b.getSample (c, i)));
    return worst;
}
Session::TrackInfo track (Session& s) { const auto snapshot = s.snapshot(); return snapshot.tracks.back(); }
bool near (float a, float b) { return std::abs (a - b) < 1e-3f; }

// A constant 0.5 signal on audio track A, played Sends Only with a 0 dB send to a return.
void sendsOnlyTests (const juce::File& root)
{
    const auto source = root.getChildFile ("half.wav");
    {
        juce::AudioBuffer<float> x (2, 4 * 48000);
        for (int c = 0; c < 2; ++c) for (int n = 0; n < x.getNumSamples(); ++n) x.setSample (c, n, .5f);
        juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (source.createOutputStream());
        auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (32));
        check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write a constant 0.5 signal");
    }
    Session s (false, makeSyntheticTestDeviceType());
    ok (s.importAudio (source, 0, 0), "put it on audio track A");
    const auto a = s.snapshot().tracks[0].id;
    ok (s.setTrackOutput (a, { Session::OutputKind::sendsOnly, {} }), "play A Sends Only");
    ok (s.addReturnTrack ("Return"), "add a return");
    const auto ret = s.snapshot().tracks.back().id;
    ok (s.setTrackSendGain (0, ret, 0), "send A to it at 0 dB");
    // The output level once playback has settled, from the last of a short run of blocks.
    const auto heard = [&]
    {
        pumpSyntheticTestDevice (s, 8);
        s.seek (.25); ok (s.play(), "play");
        const auto out = captureSyntheticTestDevice (s, 40);
        s.stop();
        return out.getSample (0, out.getNumSamples() - 1);
    };
    // The first block of playback that is heard, or -1 after 200. Each block dispatches messages
    // for at least a millisecond, so this allows the engine's file cache a fraction of a second
    // to open a new freeze, which under load can miss the first block. Without help it took 4-7 s.
    const auto firstHeard = [&]
    {
        pumpSyntheticTestDevice (s, 8);
        s.seek (.25); ok (s.play(), "play");
        int first = -1;
        for (int block = 0; block < 200 && first < 0; ++block)
            if (pumpSyntheticTestDevice (s, 1) > .4f) first = block;
        s.stop();
        return first;
    };
    const auto frozen = [&] { const auto t = s.snapshot().tracks[0]; return t.frozen && t.output.kind == Session::OutputKind::sendsOnly; };
    const auto unfrozen = [&] { const auto t = s.snapshot().tracks[0]; return ! t.frozen && t.output.kind == Session::OutputKind::sendsOnly; };
    check (near (heard(), .5f), "A is heard through its send");
    ok (s.setTrackFrozen (a, true), "freeze the Sends Only track");
    check (frozen(), "A is frozen and still plays Sends Only");
    const auto start = firstHeard();
    check (start >= 0, "the frozen track is heard through its send at once (from block " + juce::String (start) + ")");
    check (near (heard(), .5f), "and goes on being heard");
    ok (s.setTrackSendGain (0, ret, -100), "turn the send off");
    check (std::abs (heard()) < 1e-6f, "with its send off, the frozen track is not heard");
    s.undo(); s.undo(); check (unfrozen(), "undoing the send and the freeze unfreezes A, still Sends Only");
    s.redo(); check (frozen(), "redo freezes the Sends Only track again");
    const auto restart = firstHeard();
    check (restart >= 0, "the refrozen track is heard at once (from block " + juce::String (restart) + ")");
    check (near (heard(), .5f), "the refrozen track is heard through its send");
    ok (s.flattenTrack (a), "flatten the frozen Sends Only track");
    const auto flat = s.snapshot().tracks[0];
    check (flat.clips.size() == 1 && flat.output.kind == Session::OutputKind::sendsOnly, "the flattened track still plays Sends Only");
    const auto audio = read (flat.clips[0].file);
    check (near (audio.getSample (0, audio.getNumSamples() / 2), .5f), "the freeze holds A's own audio");
    check (near (heard(), .5f), "the flattened track is heard through its send");
    s.undo(); ok (s.setTrackFrozen (a, true), "undo Flatten and freeze A again");
    ok (s.setTrackSendGain (0, ret, -100), "turn the send off");
    const auto b = s.snapshot().tracks[1].id;
    // The engine's frozen-track graph does not feed the Post Mixer tap, so B takes A Post FX.
    ok (s.setTrackInputTrack (b, a, Session::InputTap::postFX), "track B takes A's audio Post FX");
    ok (s.setTrackMonitor (1, Session::MonitorMode::on), "monitor B In");
    check (near (heard(), .5f), "B hears the frozen Sends Only track");
}
}

int runFreezeTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("freeze-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto source = root.getChildFile ("source.wav");
        {
            juce::AudioBuffer<float> a (2, 36000);
            for (int n = 0; n < a.getNumSamples(); ++n) { a.setSample (0, n, (float) (.4 * std::sin (n * .021))); a.setSample (1, n, (float) (.3 * std::cos (n * .017))); }
            juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (source.createOutputStream());
            auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (32));
            check (writer && writer->writeFromAudioSampleBuffer (a, 0, a.getNumSamples()), "write a generated source");
        }
        Session s (false);
        check (s.setTrackFrozen (s.snapshot().tracks[0].id, true).failed(), "a track without clips has nothing to freeze");
        ok (s.addMidiTrack ("Synth"), "add a MIDI track");
        const auto id = track (s).id;
        ok (s.addMidiClip (id, 0, 4), "add a clip");
        { const auto snapshot = s.snapshot(); ok (s.addMidiNote (snapshot.tracks.back().clips[0].id, 60, 0, 1.5, 1), "add a note"); }
        ok (s.setInstrumentSample (id, source), "play the source from a Sampler");
        auto settings = track (s).instrument.effect;
        settings.values[instruments::sampler::velocityAmp] = 0; settings.values[instruments::sampler::ampAttack] = 0; settings.values[instruments::sampler::ampSustain] = 1;
        ok (s.setNativeEffectSettings (track (s).instrument.id, settings), "unity Sampler playback");
        const auto live = root.getChildFile ("live.wav"), render = root.getChildFile ("render.wav");
        ok (s.exportWav (live, rate, 32, .5), "render the live track");
        const auto reference = read (live);
        check (reference.getMagnitude (0, reference.getNumSamples()) > .3, "the live render is audible");
        check (s.flattenTrack (id).failed(), "an unfrozen track cannot be flattened");
        ok (s.setTrackFrozen (id, true), "freeze the track");
        check (track (s).frozen, "the track reports that it is frozen");
        ok (s.exportWav (render, rate, 32, .5), "render the frozen track");
        const auto frozen = read (render);
        check (difference (frozen, reference) < 1e-5, "the frozen render matches the live render (max difference " + juce::String (difference (frozen, reference), 8) + ")");
        auto changed = settings; changed.values[instruments::sampler::transpose] = 7;
        check (s.setNativeEffectSettings (track (s).instrument.id, changed).failed() && s.addNativeEffect ((int) s.snapshot().tracks.size() - 1, Session::EffectKind::saturator).failed()
               && s.setInstrumentSample (id, source).failed() && s.addMidiClip (id, 8, 4).failed(), "devices, instruments and clips are locked while frozen");
        check (s.addMidiNote (track (s).clips[0].id, 64, 2, 1, 1).failed(), "notes are locked while frozen");
        ok (s.setTrackGain ((int) s.snapshot().tracks.size() - 1, -6), "the mixer stays live while frozen");
        ok (s.exportWav (render, rate, 32, .5), "render the frozen track at -6 dB");
        check (difference (read (render), reference, (float) std::pow (10.0, -6 / 20.0)) < 1e-5, "the live fader scales the frozen render");
        s.undo(); s.undo(); check (! track (s).frozen, "undoing the fader and the freeze unfreezes the track");
        s.redo(); check (track (s).frozen, "redo freezes the track again");
        ok (s.exportWav (render, rate, 32, .5), "render after refreezing");
        check (difference (read (render), reference) < 1e-5, "the refrozen render matches the live render");
        ok (s.flattenTrack (id), "flatten the frozen track");
        auto flat = track (s);
        check (! flat.isMidi && ! flat.frozen && ! flat.instrument.isNativeEffect && flat.clips.size() == 1 && flat.clips[0].isAudio
               && flat.clips[0].file.existsAsFile() && flat.id == id, "flattening leaves one audio clip on the same track without devices");
        ok (s.exportWav (render, rate, 32, .5), "render the flattened track");
        check (difference (read (render), reference) < 1e-5, "the flattened clip renders like the live track");
        s.undo();
        flat = track (s);
        check (flat.isMidi && ! flat.frozen && flat.instrument.isNativeEffect && flat.clips.size() == 1 && flat.clips[0].isMidi && flat.clips[0].notes.size() == 1,
               "one undo restores the MIDI track with its Sampler and notes, unfrozen");
        ok (s.setTrackFrozen (id, true), "the restored track can be frozen again");
        ok (s.setTrackFrozen (id, false), "unfreeze the track");
        ok (s.setNativeEffectSettings (track (s).instrument.id, changed), "devices are editable again after unfreezing");
        ok (s.addReturnTrack ("Room"), "add a return");
        check (s.setTrackFrozen (s.snapshot().tracks.back().id, true).failed(), "returns cannot be frozen");
        sendsOnlyTests (root);
        std::cout << "Freeze passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL freeze: " << e.what() << std::endl; return 1; }
}
}
