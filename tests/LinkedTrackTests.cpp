#include "Session.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

// Linked-track editing: linking and unlinking with undo; a split, move, trim, fade, deactivation
// and deletion on one linked track doing the same to the clips that start and end with the edited
// one, and nothing to unlinked tracks or unmatched clips; time-selection edits; a linked MIDI
// track split with the audio; a frozen linked track refusing the whole edit; arming, recording,
// comping and take lane edits across two linked tracks fed by two mono inputs; duplicates starting
// unlinked; links reopening from a saved project, and an invalid link refused on load.
namespace halftrack
{
void useHardClipEdges();
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS linked tracks: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
bool near (double a, double b) { return std::abs (a - b) < 1e-3; }
constexpr int blocksPerSecond = 48000 / 256;
void tone (const juce::File& file, double seconds)
{
    juce::AudioBuffer<float> x (2, (int) (seconds * 48000));
    for (int c = 0; c < 2; ++c) for (int n = 0; n < x.getNumSamples(); ++n) x.setSample (c, n, (float) (.2 * std::sin (n * .05)));
    juce::WavAudioFormat format; std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (24));
    check (writer && writer->writeFromAudioSampleBuffer (x, 0, x.getNumSamples()), "write " + file.getFileName());
}
}

int runLinkedTrackTests (const juce::File& artifacts)
{
    try
    {
        useHardClipEdges(); // Renders here are compared sample for sample with hard clip edges.
        const auto root = artifacts.getChildFile ("linked-tracks-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const auto source = root.getChildFile ("tone.wav"); tone (source, 2);
        const auto project = root.getChildFile ("Linked").getChildFile ("Linked.halftrack");
        Session s (false, makeSyntheticTestDeviceType());
        ok (s.saveProject (project), "save a recording destination");
        for (const auto* name : { "Kick", "Snare", "Room" }) ok (s.addAudioTrack (name), "add an audio track");
        ok (s.addMidiTrack ("Keys"), "add a MIDI track");
        const auto index = [&] (const juce::String& name)
        {
            const auto tracks = s.snapshot().tracks;
            for (int i = 0; i < (int) tracks.size(); ++i) if (tracks[(size_t) i].name == name) return i;
            throw std::runtime_error (("no track " + name).toStdString());
        };
        const auto track = [&] (const juce::String& name) { return s.snapshot().tracks[(size_t) index (name)]; };
        // A track's clip starting at a time, or a clip with an empty id.
        const auto clipAt = [&] (const juce::String& name, double start)
        {
            for (const auto& c : track (name).clips) if (near (c.start, start)) return c;
            return Session::ClipInfo {};
        };
        const auto clipIndex = [&] (const juce::String& name, double start)
        {
            const auto clips = track (name).clips;
            for (int i = 0; i < (int) clips.size(); ++i) if (near (clips[(size_t) i].start, start)) return i;
            return -1;
        };
        const auto kickId = track ("Kick").id, snareId = track ("Snare").id, roomId = track ("Room").id, keysId = track ("Keys").id;
        for (const auto* name : { "Kick", "Snare", "Room" }) ok (s.importAudio (source, index (name), 0), "a clip at 0 s");
        ok (s.importAudio (source, index ("Kick"), 4), "and one at 4 s on Kick only");

        // Linking.
        juce::String link;
        check (s.linkTracks ({ kickId }, link).failed(), "one track cannot be linked alone");
        juce::String returnId;
        for (const auto& t : s.snapshot().tracks) if (t.isReturn) returnId = t.id;
        if (returnId.isEmpty()) { ok (s.addReturnTrack ("Return"), "add a return"); returnId = s.snapshot().tracks.back().id; }
        check (s.linkTracks ({ kickId, returnId }, link).failed(), "a return track cannot be linked");
        ok (s.linkTracks ({ kickId, snareId }, link), "link Kick and Snare");
        check (link.isNotEmpty() && track ("Kick").linkId == link && track ("Snare").linkId == link && track ("Room").linkId.isEmpty(), "both show the link, Room none");
        s.undo();
        check (track ("Kick").linkId.isEmpty() && track ("Snare").linkId.isEmpty(), "undo unlinks");
        s.redo();
        check (track ("Kick").linkId == link && track ("Snare").linkId == link, "redo links again");

        // Split.
        juce::String right;
        ok (s.splitAudioClip (clipAt ("Kick", 0).id, 1, right), "split Kick's clip at 1 s");
        check (track ("Kick").clips.size() == 3 && track ("Snare").clips.size() == 2 && clipAt ("Snare", 1).id.isNotEmpty() && track ("Room").clips.size() == 1,
               "Snare's clip is split at 1 s too, and Room's is not");
        s.undo();
        check (track ("Kick").clips.size() == 2 && track ("Snare").clips.size() == 1, "one undo joins both");
        s.redo();

        // Move.
        ok (s.moveAudioClip (clipAt ("Kick", 1).id, kickId, 1.5), "move Kick's right half to 1.5 s");
        check (clipAt ("Snare", 1.5).id.isNotEmpty() && clipAt ("Snare", 1).id.isEmpty() && near (clipAt ("Room", 0).length, 2), "Snare's right half moves with it");
        ok (s.moveAudioClip (clipAt ("Kick", 4).id, kickId, 5), "move Kick's unmatched clip");
        check (track ("Snare").clips.size() == 2 && clipAt ("Snare", 1.5).id.isNotEmpty(), "a clip without a match moves alone");
        ok (s.moveClip (index ("Snare"), clipIndex ("Snare", 1.5), 2), "move from Snare by index");
        check (clipAt ("Kick", 2).id.isNotEmpty(), "Kick's follows a move made on Snare");
        ok (s.moveAudioClip (clipAt ("Kick", 2).id, roomId, 3), "move Kick's clip to another track");
        check (clipAt ("Room", 3).id.isNotEmpty() && clipAt ("Snare", 2).id.isNotEmpty(), "a move to another track leaves the linked clip");
        s.undo();

        // Trim.
        ok (s.setAudioClipRange (clipAt ("Kick", 0).id, .1, .8, .1), "trim Kick's left half");
        auto trimmed = clipAt ("Snare", .1);
        check (trimmed.id.isNotEmpty() && near (trimmed.length, .8) && near (trimmed.sourceOffset, .1), "Snare's left half is trimmed the same");

        // Fades: Snare takes Kick's fades, keeping its own gain.
        auto settings = clipAt ("Kick", .1).audio; settings.gainDb = -6; settings.fadeInSeconds = .05; settings.fadeInCurve = Session::FadeCurve::sCurve;
        ok (s.setAudioClipSettings (clipAt ("Kick", .1).id, settings), "a fade-in and gain on Kick");
        auto snare = clipAt ("Snare", .1).audio;
        check (near (snare.fadeInSeconds, .05) && snare.fadeInCurve == Session::FadeCurve::sCurve && snare.gainDb == 0, "Snare gets the fade-in but not the gain");
        settings = clipAt ("Snare", .1).audio; settings.fadeOutSeconds = .2;
        ok (s.setAudioClipSettings (clipAt ("Snare", .1).id, settings), "a fade-out on Snare");
        check (near (clipAt ("Kick", .1).audio.fadeOutSeconds, .2) && clipAt ("Kick", .1).audio.gainDb == -6, "Kick gets Snare's fade-out");
        s.undo();
        check (near (clipAt ("Kick", .1).audio.fadeOutSeconds, 0) && near (clipAt ("Snare", .1).audio.fadeOutSeconds, 0), "one undo removes both fade-outs");
        s.redo();

        // Activation and deletion.
        ok (s.setClipActive (clipAt ("Kick", .1).id, false), "deactivate Kick's clip");
        check (! clipAt ("Snare", .1).active && clipAt ("Room", 0).active, "Snare's deactivates, Room's does not");
        ok (s.setClipActive (clipAt ("Snare", .1).id, true), "activate from Snare");
        check (clipAt ("Kick", .1).active, "Kick's activates");
        ok (s.removeClip (index ("Kick"), clipIndex ("Kick", .1)), "delete Kick's clip");
        check (clipAt ("Snare", .1).id.isEmpty() && clipAt ("Room", 0).id.isNotEmpty(), "Snare's is deleted with it");
        s.undo();
        check (clipAt ("Kick", .1).id.isNotEmpty() && clipAt ("Snare", .1).id.isNotEmpty(), "one undo restores both");

        // A frozen linked track refuses the whole edit.
        ok (s.setTrackFrozen (snareId, true), "freeze Snare");
        check (s.removeClip (index ("Kick"), clipIndex ("Kick", .1)).failed() && clipAt ("Kick", .1).id.isNotEmpty(), "deleting a clip linked to a frozen one is refused");
        ok (s.setTrackFrozen (snareId, false), "unfreeze Snare");

        // Time-selection edits take in the linked tracks.
        ok (s.deleteRange ({ kickId }, 1.9, 3.1), "delete time on Kick");
        check (clipAt ("Kick", 2).id.isEmpty() && clipAt ("Snare", 2).id.isEmpty() && near (clipAt ("Room", 0).length, 2), "Snare's clip in the range goes too, Room keeps its");

        // A linked MIDI track splits with the audio.
        ok (s.addMidiClip (keysId, 0, 4), "a MIDI clip under the audio");
        ok (s.linkTracks ({ kickId, snareId, keysId }, link), "link Keys as well");
        ok (s.splitAudioClip (clipAt ("Kick", .1).id, .5, right), "split Kick at 0.5 s");
        check (track ("Keys").clips.size() == 2 && clipAt ("Keys", .5).id.isNotEmpty() && clipAt ("Snare", .5).id.isNotEmpty(), "Keys and Snare split there too");

        // Duplicates start unlinked; unlinking a track leaves the others linked until one remains.
        juce::String copy;
        ok (s.duplicateTrack (snareId, copy), "duplicate Snare");
        const auto duplicated = s.snapshot();
        const auto duplicate = std::find_if (duplicated.tracks.begin(), duplicated.tracks.end(), [&] (const auto& t) { return t.id == copy; });
        check (duplicate != duplicated.tracks.end() && duplicate->name == "Snare" && duplicate->linkId.isEmpty() && track ("Snare").linkId == link,
               "the duplicate keeps the name, as the reference's does, and starts unlinked");
        s.undo();
        ok (s.unlinkTracks ({ keysId }), "unlink Keys");
        check (track ("Keys").linkId.isEmpty() && track ("Kick").linkId == link && track ("Snare").linkId == link, "Kick and Snare stay linked");
        ok (s.unlinkTracks ({ kickId }), "unlink Kick");
        check (track ("Kick").linkId.isEmpty() && track ("Snare").linkId.isEmpty(), "a link left with one track ends");
        s.undo();
        check (track ("Kick").linkId == link && track ("Snare").linkId == link, "undo links them again");

        // Arming, recording, take lanes and comping across two linked tracks with their own inputs.
        ok (s.setAudioInputPairStereo (0, false), "split the device's pair into mono inputs");
        const auto inputs = s.audioInputGroups();
        check (inputs.size() >= 2, "two mono inputs");
        ok (s.setTrackInput (index ("Kick"), inputs[0].name), "Kick takes the first");
        ok (s.setTrackInput (index ("Snare"), inputs[1].name), "Snare the second");
        ok (s.setTrackArmed (index ("Kick"), true), "arm Kick");
        check (track ("Kick").armed && track ("Snare").armed && ! track ("Room").armed, "Snare arms with it; Room has no input and stays");
        pumpSyntheticTestDevice (s, 8);
        for (int pass = 0; pass < 2; ++pass)
        {
            s.seek (6); ok (s.record(), "record a pass");
            pumpSyntheticTestDevice (s, blocksPerSecond);
            s.stop();
        }
        auto kickLanes = track ("Kick").takeLanes, snareLanes = track ("Snare").takeLanes;
        check (kickLanes.size() == 2 && snareLanes.size() == 2 && ! kickLanes[0].clips.empty() && ! snareLanes[0].clips.empty(), "two passes give each track two take lanes");
        ok (s.compTake (kickLanes[0].id, 6.25, 6.5), "comp Kick's Take 1 from 6.25 to 6.5 s");
        const auto playing = [&] (const juce::String& name, double at)
        {
            for (const auto& c : track (name).clips) if (c.start <= at && c.start + c.length > at) return c.file;
            return juce::File();
        };
        check (playing ("Kick", 6.3) == kickLanes[0].clips[0].file && playing ("Snare", 6.3) == snareLanes[0].clips[0].file
                   && playing ("Kick", 6.7) == kickLanes[1].clips[0].file && playing ("Snare", 6.7) == snareLanes[1].clips[0].file,
               "Snare comps its own Take 1 in the same range");
        s.undo();
        check (playing ("Snare", 6.3) == snareLanes[1].clips[0].file, "one undo restores both main lanes");
        juce::String lane;
        ok (s.insertTakeLane (kickId, lane), "insert a take lane on Kick");
        check (track ("Kick").takeLanes.size() == 3 && track ("Snare").takeLanes.size() == 3, "Snare gets one too");
        ok (s.renameTakeLane (lane, "Spare"), "rename it");
        check (track ("Snare").takeLanes[2].name == "Spare", "Snare's is renamed");
        ok (s.removeTakeLane (track ("Snare").takeLanes[2].id), "delete Snare's");
        check (track ("Kick").takeLanes.size() == 2 && track ("Snare").takeLanes.size() == 2, "Kick's goes too");
        ok (s.setTrackArmed (index ("Snare"), false), "disarm Snare");
        check (! track ("Kick").armed && ! track ("Snare").armed, "Kick disarms with it");
        ok (s.setTrackInput (index ("Kick"), {}), "disconnect Kick"); ok (s.setTrackInput (index ("Snare"), {}), "disconnect Snare");
        ok (s.setAudioInputPairStereo (0, true), "join the pair again");

        // Links reopen; an invalid one is refused, keeping the open project.
        ok (s.saveProject (project), "save");
        {
            Session reopened (false);
            ok (reopened.loadProject (project), "reopen");
            juce::String kick, snareLink;
            for (const auto& t : reopened.snapshot().tracks) { if (t.name == "Kick") kick = t.linkId; if (t.name == "Snare") snareLink = t.linkId; }
            check (kick == link && snareLink == link, "the link reopens");
            const auto damaged = project.getSiblingFile ("Damaged.halftrack");
            check (damaged.replaceWithText (project.loadFileAsString().replace (link, "not a link")), "write a project with a spaced link");
            const auto before = reopened.snapshot().tracks.size();
            check (reopened.loadProject (damaged).failed() && reopened.snapshot().tracks.size() == before, "a link with spaces is refused, keeping the open project");
        }
        std::cout << "Linked tracks passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL linked tracks: " << e.what() << std::endl; return 1; }
}
}
