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
void useHardClipEdges();
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

// Bounce Track in Place, as the reference's: an audio track's render, with its Delay's echoes,
// replaces its clip and devices, cut at the clip's end, the echoes in a tail clip after it.
void bounceTests (const juce::File& root)
{
    const auto burst = root.getChildFile ("burst.wav");
    {
        juce::AudioBuffer<float> x (2, (int) (rate / 5));
        for (int c = 0; c < 2; ++c) for (int n = 0; n < x.getNumSamples(); ++n) x.setSample (c, n, (float) (.5 * std::sin (n * 2 * juce::MathConstants<double>::pi * 440 / rate)));
        juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (burst.createOutputStream());
        auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (32));
        check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write a 0.2 second burst");
    }
    Session s (false);
    ok (s.importAudio (burst, 0, 0), "put it on audio track A");
    const auto a = s.snapshot().tracks[0].id;
    ok (s.addNativeEffect (0, Session::EffectKind::delay), "echo it with a Delay");
    const auto live = root.getChildFile ("bounce-live.wav"), bounced = root.getChildFile ("bounce.wav");
    ok (s.exportWav (live, rate, 32, 3), "render it live, with three seconds after it");
    const auto reference = read (live);
    check (s.canBounceTracksInPlace ({ a }), "it can be bounced");
    ok (s.bounceTracksInPlace ({ a }), "bounce it in place");
    const auto t = s.snapshot().tracks[0];
    check (t.plugins.empty() && ! t.frozen && t.clips.size() == 2 && std::abs (t.clips[0].start) < 1e-6 && std::abs (t.clips[0].length - .2) < 1e-3
               && t.clips[0].name.endsWith ("Bounce") && std::abs (t.clips[1].start - .2) < 1e-3 && t.clips[1].length > .5 && t.clips[1].name.endsWith ("Bounce Tail"),
           "the Delay goes, and the render is cut at the clip's end, its echoes in a tail clip: " + [&]
           {
               juce::String text; text << (int) t.plugins.size() << " devices, frozen " << (int) t.frozen << ";";
               for (const auto& c : t.clips) text << " [" << c.name << " " << c.start << "+" << c.length << "]";
               return text;
           }());
    ok (s.exportWav (bounced, rate, 32, 3), "render the bounced track");
    // The render stops once the echoes stay under the engine's -70 dB silence, so the last of them, below it, are not kept.
    check (difference (read (bounced), reference) < juce::Decibels::decibelsToGain (-70.0f), "it plays as the live track did, but for echoes below -70 dB (max difference "
           + juce::String (difference (read (bounced), reference), 8) + ")");
    s.undo();
    check (s.snapshot().tracks[0].plugins.size() == 1 && s.snapshot().tracks[0].clips.size() == 1 && ! s.snapshot().tracks[0].frozen, "one undo restores the clip and the Delay, unfrozen");
    s.redo();
    check (s.snapshot().tracks[0].clips.size() == 2 && s.snapshot().tracks[0].plugins.empty(), "redo bounces it again, without rendering");
    s.undo();
    {
        // Bounce to New Track: the first second's render goes to a new track after A, at A's level; A
        // keeps its Delay, its clip deactivated.
        ok (s.setTrackGain (0, -6), "A at -6 dB");
        const auto count = s.snapshot().tracks.size();
        juce::StringArray made;
        check (s.canBounceToNewTracks ({ a }, 0, 1) && ! s.canBounceToNewTracks ({ a }, 5, 6) && ! s.canBounceToNewTracks ({ a }, 1, 0), "it can bounce a range holding an active clip");
        ok (s.bounceToNewTracks ({ a }, 0, 1, made), "bounce A's first second to a new track");
        const auto shot = s.snapshot();
        const auto& n = shot.tracks[1];
        check (made.size() == 1 && shot.tracks.size() == count + 1 && n.id == made[0] && n.plugins.empty() && n.name.endsWith ("Bounce") && std::abs (n.gainDb + 6) < 1e-3
                   && n.clips.size() == 2 && std::abs (n.clips[0].length - .2) < 1e-3 && std::abs (n.clips[1].start - .2) < 1e-3 && n.clips[1].start + n.clips[1].length <= 1 + 1e-6,
               "a new track after A, at A's level and without devices, holds the burst and its echoes to the second's end");
        check (shot.tracks[0].plugins.size() == 1 && ! shot.tracks[0].clips[0].active, "A keeps its Delay, its clip deactivated");
        const auto onNew = root.getChildFile ("bounce-new.wav");
        ok (s.exportWav (onNew, rate, 32, 0), "render the new track's second");
        const auto heard = read (onNew);
        const auto scale = (float) std::pow (10.0, -6 / 20.0);
        double worst = 0;
        for (int c = 0; c < 2; ++c) for (int i = 0; i < std::min ((int) rate, std::min (heard.getNumSamples(), reference.getNumSamples())); ++i)
            worst = std::max (worst, (double) std::abs (heard.getSample (c, i) - scale * reference.getSample (c, i)));
        check (worst < juce::Decibels::decibelsToGain (-70.0f), "it plays the first second as A did, 6 dB down (max difference " + juce::String (worst, 8) + ")");
        s.undo();
        check (s.snapshot().tracks.size() == count && s.snapshot().tracks[0].clips.size() == 1 && s.snapshot().tracks[0].clips[0].active, "one undo removes the new track and reactivates the clip");
        s.undo();
        check (s.bounceToNewTracks ({ a }, 5, 6, made).failed() && s.bounceToNewTracks ({ a }, 1, 0, made).failed(), "a range without active clips, or a backward one, is refused");
        ok (s.bounceToNewTracks ({ a }, .1, 1, made), "bounce from inside A's clip");
        {
            const auto split = s.snapshot();
            const auto& kept = split.tracks[0].clips; const auto& bounced = split.tracks[1].clips;
            check (kept.size() == 2 && kept[0].active && std::abs (kept[0].length - .1) < 1e-3 && ! kept[1].active && std::abs (kept[1].start - .1) < 1e-3
                       && bounced.size() == 2 && std::abs (bounced[0].start - .1) < 1e-3 && std::abs (bounced[0].length - .1) < 1e-3,
                   "A's clip is split at the range's start, the part inside deactivated, and the new track's clips start there");
        }
        s.undo();
        check (s.snapshot().tracks[0].clips.size() == 1 && s.snapshot().tracks[0].clips[0].active, "undo puts A's clip back whole");
        ok (s.setTrackFrozen (a, true), "freeze A");
        check (s.canBounceToNewTracks ({ a }, 0, 1) && s.bounceToNewTracks ({ a }, 0, 1, made).failed(), "a frozen track is offered but refused: its one render would play the deactivated clips on");
        s.undo();
    }
    ok (s.addReturnTrack ("Room"), "add a return");
    const auto room = s.snapshot().tracks.back().id;
    check (! s.canBounceTracksInPlace ({ room }) && s.bounceTracksInPlace ({ room }).failed() && s.bounceTracksInPlace ({}).failed(), "returns, and nothing, are not bounced");
}
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
        useHardClipEdges(); // Renders here are compared sample for sample with hard clip edges.
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
        ok (s.setNativeEffectSettings (track (s).instrument.id, settings), "unity Sampler playback again");
        // A Session clip playing the same note, in Gate mode, which keeps its colour when the track changes colour.
        ok (s.addMidiClipToSlot (id, 0, 4), "add a Session clip");
        const auto sessionClip = [&s] { return track (s).slots.at (0).clip; };
        ok (s.addMidiNote (sessionClip().id, 60, 0, 1.5, 1), "the same note in it");
        { auto launch = sessionClip().launch; launch.mode = Session::LaunchMode::gate; ok (s.setClipLaunchSettings (sessionClip().id, launch), "Gate mode"); }
        const auto clipColour = sessionClip().colour;
        ok (s.setTrackColours ({ id }, juce::Colours::teal), "a new track colour, the clip keeping the one it showed");
        const bool looped = sessionClip().loop.on;
        ok (s.bounceTracksInPlace ({ id }), "bounce the MIDI track in place");
        const auto bouncedTrack = track (s);
        check (! bouncedTrack.isMidi && ! bouncedTrack.frozen && ! bouncedTrack.instrument.isNativeEffect && bouncedTrack.clips.size() == 1 && bouncedTrack.clips[0].isAudio
                   && bouncedTrack.id == id && bouncedTrack.name == track (s).name, "it becomes an audio track, the render in place of its clip");
        ok (s.exportWav (render, rate, 32, .5), "render the bounced track");
        check (difference (read (render), reference) < 1e-5, "the bounced clip renders like the live track");
        {
            const auto& slot = bouncedTrack.slots.at (0);
            check (slot.hasClip && slot.clip.isAudio && slot.clip.name.endsWith ("Bounce") && slot.clip.launch.mode == Session::LaunchMode::gate && slot.clip.loop.on == looped
                       && slot.clip.colour == clipColour && slot.clip.warp.mode != Session::WarpMode::off,
                   "the Session clip's render takes its slot, warped, with its launch mode, looping and colour");
            check (difference (read (slot.clip.file), reference) < 1e-5, "the Session clip's render sounds as the same note in the Arrangement did (max difference "
                   + juce::String (difference (read (slot.clip.file), reference), 8) + ")");
        }
        s.undo();
        check (track (s).isMidi && track (s).instrument.isNativeEffect && track (s).clips.size() == 1 && track (s).clips[0].notes.size() == 1 && ! track (s).frozen
                   && sessionClip().isMidi && sessionClip().notes.size() == 1 && sessionClip().launch.mode == Session::LaunchMode::gate,
               "one undo restores the MIDI track with its Sampler, notes and Session clip, unfrozen");
        {
            juce::StringArray made;
            ok (s.bounceToNewTracks ({ id }, 0, 2, made), "bounce the MIDI track's clip to a new track");
            const auto shot = s.snapshot();
            const auto& n = shot.tracks.back(); const auto& midi = shot.tracks[shot.tracks.size() - 2];
            check (made.size() == 1 && n.id == made[0] && ! n.isMidi && ! n.instrument.isNativeEffect && ! n.clips.empty() && n.clips[0].isAudio
                       && midi.id == id && midi.instrument.isNativeEffect && midi.clips.size() == 1 && ! midi.clips[0].active,
                   "a new audio track after the MIDI track holds its render; the MIDI track keeps its Sampler, its clip deactivated");
            ok (s.exportWav (render, rate, 32, .5), "render the new track");
            check (difference (read (render), reference) < 1e-5, "the new track plays as the MIDI track did (max difference " + juce::String (difference (read (render), reference), 8) + ")");
            check (! n.slots.at (0).hasClip && midi.slots.at (0).hasClip && midi.slots.at (0).clip.active, "a time range leaves the Session clips alone");
            s.undo();
            check (track (s).id == id && track (s).clips[0].active, "one undo removes the new track");
        }
        {
            // Bounce to New Track from the Session slot.
            juce::StringArray made;
            const auto clipId = sessionClip().id;
            check (s.canBounceSlotsToNewTracks ({ clipId }) && ! s.canBounceSlotsToNewTracks ({ "none" }), "the Session clip can be bounced to a new track");
            ok (s.bounceSlotsToNewTracks ({ clipId }, made), "bounce the Session clip to a new track");
            const auto shot = s.snapshot();
            const auto& n = shot.tracks.back(); const auto& midi = shot.tracks[shot.tracks.size() - 2];
            check (made.size() == 1 && n.id == made[0] && ! n.isMidi && n.clips.empty() && n.slots.at (0).hasClip && n.slots.at (0).clip.isAudio
                       && n.slots.at (0).clip.launch.mode == Session::LaunchMode::gate && midi.id == id && midi.instrument.isNativeEffect && ! midi.slots.at (0).clip.active
                       && midi.clips.at (0).active,
                   "a new audio track after the MIDI track holds the clip's render in the same slot; the clip is deactivated, the Sampler and Arrangement clip untouched");
            check (difference (read (n.slots.at (0).clip.file), reference) < 1e-5, "the new track's clip holds the Session clip's sound");
            check (! s.canBounceSlotsToNewTracks ({ clipId }), "a deactivated clip is not offered");
            s.undo();
            check (track (s).id == id && sessionClip().active, "one undo removes the new track and reactivates the clip");
        }
        ok (s.addReturnTrack ("Room"), "add a return");
        check (s.setTrackFrozen (s.snapshot().tracks.back().id, true).failed(), "returns cannot be frozen");
        {
            // A release ringing past the clip's end: instruments report a tail, so the freeze keeps it.
            ok (s.addMidiTrack ("Release"), "add a MIDI track to hold a note to its clip's end");
            juce::String held;
            for (const auto& t : s.snapshot().tracks) if (t.userName == "Release") held = t.id;
            const auto heldTrack = [&s, &held] { for (const auto& t : s.snapshot().tracks) if (t.id == held) return t; return Session::TrackInfo {}; };
            ok (s.addMidiClip (held, 0, 1), "a one-beat clip");
            ok (s.addMidiNote (heldTrack().clips.at (0).id, 60, 0, 1, 1), "a note to its end");
            ok (s.setInstrumentSample (held, source), "played by a Sampler");
            auto ringing = heldTrack().instrument.effect;
            ringing.values[instruments::sampler::velocityAmp] = 0; ringing.values[instruments::sampler::ampAttack] = 0; ringing.values[instruments::sampler::ampSustain] = 1;
            ringing.values[instruments::sampler::ampRelease] = 400;
            ok (s.setNativeEffectSettings (heldTrack().instrument.id, ringing), "with a 400 ms release");
            const auto shot = s.snapshot();
            for (int t = 0; t < (int) shot.tracks.size(); ++t) if (shot.tracks[(size_t) t].id != held) ok (s.setTrackMute (t, true), "mute the others");
            const auto liveRelease = root.getChildFile ("release-live.wav"), frozenRelease = root.getChildFile ("release-frozen.wav");
            ok (s.exportWav (liveRelease, rate, 32, 1), "render it live with a second after it");
            const auto live = read (liveRelease);
            check (live.getMagnitude ((int) (rate * .55), (int) (rate * .2)) > .01f, "the release sounds after the clip's half second");
            // Exports render a frozen track from its clips, so the freeze itself is heard through a bounce.
            ok (s.setTrackFrozen (held, true), "freeze it");
            ok (s.bounceTracksInPlace ({ held }), "bounce it in place from its freeze");
            ok (s.exportWav (frozenRelease, rate, 32, 1), "render the bounce");
            check (heldTrack().clips.size() == 2 && heldTrack().clips[1].name.endsWith ("Bounce Tail")
                       && difference (read (frozenRelease), live) < juce::Decibels::decibelsToGain (-70.0f),
                   "the freeze keeps the release, which the bounce puts in a tail clip (max difference " + juce::String (difference (read (frozenRelease), live), 8) + ")");
            s.undo();
            check (heldTrack().isMidi && heldTrack().clips.size() == 1 && ! heldTrack().frozen, "undo restores the MIDI track, unfrozen");
            // The same note in a looping Session clip bounces to one loop, the release wrapped onto its start
            // so it loops on seamlessly; unlooped, the release follows the clip.
            ok (s.addMidiClipToSlot (held, 0, 1), "a one-beat Session clip");
            const auto slotClip = [&heldTrack] { return heldTrack().slots.at (0).clip; };
            ok (s.addMidiNote (slotClip().id, 60, 0, 1, 1), "the note to its end");
            ok (s.setClipLoop (slotClip().id, { true, 0, 1 }, 0), "looping over its beat");
            juce::StringArray made;
            const auto bouncedRender = [&s, &made]
            {
                for (const auto& t : s.snapshot().tracks) if (! made.isEmpty() && t.id == made[0]) return read (t.slots.at (0).clip.file);
                throw std::runtime_error ("the bounced track is missing");
            };
            ok (s.bounceSlotsToNewTracks ({ slotClip().id }, made), "bounce the looping clip to a new track");
            const int loop = (int) (rate / 2);
            juce::AudioBuffer<float> folded (2, loop); folded.clear();
            for (int c = 0; c < 2; ++c) for (int i = 0; i < live.getNumSamples(); ++i) folded.addSample (c, i % loop, live.getSample (c, i));
            const auto looped = bouncedRender();
            check (looped.getNumSamples() == loop && difference (looped, folded) < juce::Decibels::decibelsToGain (-70.0f),
                   "looping, it renders one loop, the release wrapped onto its start (" + juce::String (looped.getNumSamples()) + " samples, max difference "
                       + juce::String (difference (looped, folded), 8) + ")");
            s.undo();
            ok (s.setClipLoop (slotClip().id, { false, 0, 1 }, 0), "unlooped");
            ok (s.bounceSlotsToNewTracks ({ slotClip().id }, made), "bounce it unlooped");
            const auto plain = bouncedRender();
            check (plain.getNumSamples() > loop && difference (plain, live) < juce::Decibels::decibelsToGain (-70.0f),
                   "unlooped, it renders the clip and then the release (max difference " + juce::String (difference (plain, live), 8) + ")");
        }
        bounceTests (root);
        sendsOnlyTests (root);
        std::cout << "Freeze passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL freeze: " << e.what() << std::endl; return 1; }
}
}
