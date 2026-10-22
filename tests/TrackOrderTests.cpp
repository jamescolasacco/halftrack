#include "Session.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace halftrack
{
namespace
{
void check (bool value, const juce::String& message)
{
    if (! value) throw std::runtime_error (message.toStdString());
    std::cout << "PASS track order: " << message << std::endl;
}
void ok (const juce::Result& result, const juce::String& message)
{ check (result.wasOk(), message + (result.failed() ? ": " + result.getErrorMessage() : "")); }
std::vector<juce::String> order (const Session::Snapshot& state)
{
    std::vector<juce::String> ids;
    for (const auto& track : state.tracks) ids.push_back (track.id);
    return ids;
}
const Session::TrackInfo& find (const Session::Snapshot& state, const juce::String& id)
{
    for (const auto& track : state.tracks) if (track.id == id) return track;
    throw std::runtime_error ("Track identity disappeared");
}
void sameClip (const Session::ClipInfo& a, const Session::ClipInfo& b)
{
    check (a.id == b.id && a.name == b.name && a.start == b.start && a.length == b.length
        && a.sourceOffset == b.sourceOffset && a.isMidi == b.isMidi && a.isAudio == b.isAudio
        && a.audio == b.audio && a.notes.size() == b.notes.size(), "clip identity and settings stay with their track");
    for (size_t n = 0; n < a.notes.size(); ++n)
        check (a.notes[n].id == b.notes[n].id && a.notes[n].pitch == b.notes[n].pitch
            && a.notes[n].startBeat == b.notes[n].startBeat && a.notes[n].lengthBeats == b.notes[n].lengthBeats
            && a.notes[n].velocity == b.notes[n].velocity, "slot note identity and content survive ordering");
}
void retained (const Session::Snapshot& before, const Session::Snapshot& after)
{
    check (before.tracks.size() == after.tracks.size(), "reordering retains every track");
    for (const auto& a : before.tracks)
    {
        const auto& b = find (after, a.id);
        check (a.name == b.name && a.colour == b.colour && a.isMidi == b.isMidi && a.isReturn == b.isReturn
            && a.gainDb == b.gainDb && a.pan == b.pan && a.mute == b.mute && a.solo == b.solo && a.armed == b.armed
            && a.monitor == b.monitor && a.inputName == b.inputName && a.sendMode == b.sendMode
            && a.plugins.size() == b.plugins.size() && a.clips.size() == b.clips.size()
            && a.sends.size() == b.sends.size() && a.slots.size() == b.slots.size(), "track controls and routing dimensions survive ordering");
        for (size_t i = 0; i < a.plugins.size(); ++i)
            check (a.plugins[i].id == b.plugins[i].id && a.plugins[i].name == b.plugins[i].name
                && a.plugins[i].enabled == b.plugins[i].enabled, "device identities and order stay with their track");
        for (const auto& send : a.sends)
        {
            const auto match = std::find_if (b.sends.begin(), b.sends.end(), [&] (const auto& other) { return other.returnTrackId == send.returnTrackId; });
            check (match != b.sends.end() && match->gainDb == send.gainDb, "send destination identity and gain survive ordering");
        }
        for (size_t i = 0; i < a.clips.size(); ++i) sameClip (a.clips[i], b.clips[i]);
        for (size_t i = 0; i < a.slots.size(); ++i)
        {
            check (a.slots[i].id == b.slots[i].id && a.slots[i].hasClip == b.slots[i].hasClip, "Session slot identity stays with its track");
            if (a.slots[i].hasClip) sameClip (a.slots[i].clip, b.slots[i].clip);
        }
    }
}
juce::AudioBuffer<float> read (const juce::File& file)
{
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    check (reader && reader->numChannels == 2 && reader->sampleRate == 48000 && reader->lengthInSamples == 4800,
        "render has expected complete stereo extent");
    juce::AudioBuffer<float> audio (2, 4800);
    check (reader->read (&audio, 0, 4800, 0, true, true), "read complete generated render"); return audio;
}
void render (Session& session, const juce::File& file, const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    ok (session.exportWav (file), "render reordered signal graph"); const auto actual = read (file);
    double worst = 0;
    // Dry gains 1/2 and 1/4. Post sends 1/4 and 1/8 feed a return at 1/2.
    for (int c = 0; c < 2; ++c) for (int n = 0; n < 4800; ++n)
    {
        const double value = actual.getSample (c, n), expected = a.getSample (c, n) * .5625 + b.getSample (c, n) * .265625;
        if (! std::isfinite (value)) throw std::runtime_error ("Nonfinite reordered graph sample");
        worst = std::max (worst, std::abs (value - expected));
    }
    check (worst < 2e-6, "every sample matches the independent dry-plus-return mix after reordering");
}
}
int runTrackOrderTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("track-order-" + juce::Uuid().toString()); ok (root.createDirectory(), "create generated fixture directory");
        const auto a = root.getChildFile ("a.wav"), b = root.getChildFile ("b.wav"), output = root.getChildFile ("mix.wav"), project = root.getChildFile ("order.halftrack");
        for (int file = 0; file < 2; ++file)
        {
            juce::AudioBuffer<float> samples (2, 4800);
            for (int c = 0; c < 2; ++c) for (int n = 0; n < 4800; ++n)
                samples.setSample (c, n, ((n * (17+file*4) + c*13) % 97 - 48) / 1024.0f);
            juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream ((file == 0 ? a : b).createOutputStream());
            auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (24));
            check (writer && writer->writeFromAudioSampleBuffer (samples, 0, 4800), "write independently generated stereo media");
        }
        const auto first = read (a), second = read (b); Session session (false);
        ok (session.importAudio (a, 0), "import first source"); ok (session.importAudio (b, 1), "import second source");
        ok (session.addMidiTrack ("MIDI slots"), "add MIDI track"); ok (session.addReturnTrack ("Shared return"), "add return track");
        auto state = session.snapshot(); const auto midi = state.tracks[2].id, ret = state.tracks[3].id;
        ok (session.addMidiClipToSlot (midi, 0, 4), "create Session MIDI content"); state = session.snapshot();
        ok (session.addMidiNote (state.tracks[2].slots[0].clip.id, 67, .5, 1, .7f), "add persistent Session note");
        ok (session.importAudioToSlot (a, 0, 1), "create Session audio content");
        const auto db = [] (float value) { return 20.0f * std::log10 (value); };
        ok (session.setTrackGain (0, db (.5f)), "set first dry level"); ok (session.setTrackGain (1, db (.25f)), "set second dry level");
        ok (session.setTrackGain (3, db (.5f)), "set return level"); ok (session.setTrackSendGain (0, ret, db (.25f)), "send first source");
        ok (session.setTrackSendGain (1, ret, db (.125f)), "send second source");
        ok (session.saveProject (project), "save initial order"); ok (session.loadProject (project), "reopen clean initial order");
        const auto baseline = session.snapshot(); const auto original = order (baseline);
        ok (session.moveTrack (original[0], 0), "same-position no-op"); check (!session.canUndo() && !session.snapshot().dirty, "no-op keeps clean history");
        render (session, output, first, second);
        for (int from = 0; from < 4; ++from) for (int to = 0; to < 4; ++to)
        {
            if (from == to) continue;
            auto expected = original; const auto id = expected[(size_t)from]; expected.erase (expected.begin()+from); expected.insert (expected.begin()+to, id);
            ok (session.moveTrack (id, to), "move between arbitrary top-level positions");
            check (order (session.snapshot()) == expected, "native track order matches requested final index"); retained (baseline, session.snapshot());
            session.undo(); check (order (session.snapshot()) == original, "one undo restores all original positions");
            check (session.moveTrack ("missing", 0).failed() && session.moveTrack (id, -1).failed() && session.moveTrack (id, 4).failed() && session.canRedo(),
                "invalid identities and indices preserve redo");
            session.redo(); check (order (session.snapshot()) == expected, "redo restores reordered identities"); session.undo();
        }
        session.beginEditGesture ("Reorder multiple tracks");
        ok (session.moveTrack (ret, 0), "return may precede its send sources"); ok (session.moveTrack (original[0], 3), "move audio source to end");
        ok (session.moveTrack (midi, 1), "move MIDI between return and audio"); session.endEditGesture();
        const auto finalOrder = order (session.snapshot()); retained (baseline, session.snapshot()); render (session, output, first, second);
        session.undo(); check (order (session.snapshot()) == original, "grouped reorder is one undo"); session.redo();
        check (order (session.snapshot()) == finalOrder, "grouped reorder redo retains order");
        ok (session.saveProject (project), "save reordered tracks and routing"); ok (session.loadProject (project), "reopen reordered tracks and routing");
        check (order (session.snapshot()) == finalOrder, "saved order is persistent"); retained (baseline, session.snapshot()); render (session, output, first, second);
        // Track heights, saved with the project outside the undo history, and moving with their tracks.
        {
            const auto id = session.snapshot().tracks[1].id;
            const bool couldUndo = session.canUndo();
            check (session.setTrackHeight (id, Session::minimumTrackHeight - 1).failed() && session.setTrackHeight (id, Session::maximumTrackHeight + 1).failed()
                       && session.setTrackHeight ("missing", 100).failed(), "heights outside 20 to 600 pixels, or for no track, are refused");
            ok (session.setTrackHeight (id, 140), "make a track 140 pixels high");
            check (session.snapshot().tracks[1].height == 140 && session.snapshot().tracks[0].height == 0 && session.canUndo() == couldUndo && ! session.snapshot().dirty,
                   "it is that high, the others default, and it is neither an undo step nor an unsaved change");
            ok (session.moveTrack (id, 0), "move it first");
            check (session.snapshot().tracks[0].id == id && session.snapshot().tracks[0].height == 140, "its height moves with it");
            ok (session.saveProject (project), "save the height"); ok (session.loadProject (project), "reopen");
            check (session.snapshot().tracks[0].id == id && session.snapshot().tracks[0].height == 140, "the height is saved with the project");
            ok (session.setTrackHeight (id, 0), "back to the default");
            check (session.snapshot().tracks[0].height == 0, "0 restores the default height");
            ok (session.setTrackFolded (id, true), "fold it");
            check (session.snapshot().tracks[0].folded && ! session.snapshot().tracks[1].folded && session.canUndo() == couldUndo, "it is folded, outside the undo history");
            ok (session.saveProject (project), "save the fold"); ok (session.loadProject (project), "reopen");
            check (session.snapshot().tracks[0].folded && session.setTrackFolded ("missing", true).failed(), "the fold is saved; a missing track is refused");
            ok (session.setTrackFolded (id, false), "unfold it");
        }
        std::cout << "Track order tests passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL track order: " << e.what() << std::endl; return 1; }
}
}
