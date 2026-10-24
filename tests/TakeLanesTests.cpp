#include "Session.h"
#include <set>
#include <cmath>
#include <iostream>
#include <stdexcept>

// Take lanes and comping, through the synthetic device. Each recorded pass on an armed track is
// kept in a take lane as well as the main lane, a new lane when the last one has material at or
// after the pass's start. Comping copies a lane's range into the main lane, replacing what was
// there, and cycling swaps in the next or previous lane. Lanes are edited with undo, and reopen
// with their audio collected.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
juce::AudioBuffer<float> captureSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS take lanes: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
constexpr int blocksPerSecond = 48000 / 256;
bool near (double a, double b) { return std::abs (a - b) < 1e-3; }
void take (Session& s, double from, double seconds)
{
    s.seek (from); ok (s.record(), "record");
    pumpSyntheticTestDevice (s, (int) std::lround (seconds * blocksPerSecond));
    s.stop();
}
std::vector<Session::ClipInfo> sorted (std::vector<Session::ClipInfo> clips)
{ std::sort (clips.begin(), clips.end(), [] (const auto& a, const auto& b) { return a.start < b.start; }); return clips; }
}

int runTakeLanesTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("take-lanes-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        Session s (false, makeSyntheticTestDeviceType());
        const auto project = root.getChildFile ("Takes").getChildFile ("Takes.halftrack");
        ok (s.saveProject (project), "save a recording destination");
        ok (s.setTrackInput (0, s.inputNames()[0]), "route the input"); ok (s.setTrackArmed (0, true), "arm the track");
        pumpSyntheticTestDevice (s, 8);
        const auto track = [&] { return s.snapshot().tracks[0]; };
        const auto trackId = track().id;
        check (track().takeLanes.empty(), "a new track has no take lanes");

        take (s, 0, 1);
        auto lanes = track().takeLanes;
        check (track().clips.size() == 1 && lanes.size() == 1 && lanes[0].name == "Take 1" && lanes[0].clips.size() == 1
               && lanes[0].clips[0].file == track().clips[0].file && near (lanes[0].clips[0].start, track().clips[0].start) && near (lanes[0].clips[0].length, track().clips[0].length)
               && lanes[0].clips[0].id != track().clips[0].id, "a recorded pass plays in the main lane and is kept in Take 1");
        const auto first = lanes[0].clips[0];
        s.undo();
        check (track().clips.empty() && track().takeLanes.empty(), "one undo removes the take from both lanes");
        s.redo();

        take (s, 0, 1);
        lanes = track().takeLanes;
        check (lanes.size() == 2 && lanes[1].name == "Take 2" && lanes[1].clips.size() == 1 && lanes[1].clips[0].file != first.file,
               "a pass over Take 1's material gets a lane of its own");
        const auto second = lanes[1].clips[0];
        take (s, 2, .5);
        lanes = track().takeLanes;
        check (lanes.size() == 2 && lanes[0].clips.size() == 2 && near (sorted (lanes[0].clips)[1].start, 2),
               "a pass after a lane's material reuses that lane");

        // Comping: the main lane holds Take 2 over the first second; comp Take 1 into 0.25-0.5 s.
        const auto laneOne = lanes[0].id, laneTwo = lanes[1].id;
        {
            // The pass in the main lane came from Take 2, so cycling back from it reaches Take 1.
            const auto at = [&] (double seconds) { for (const auto& c : track().clips) if (c.start <= seconds + 1e-9 && c.start + c.length > seconds) return c.file; return juce::File(); };
            ok (s.cycleTake (trackId, .25, .5, false), "cycle back from the pass just recorded");
            check (at (.3) == first.file, "it reaches Take 1, not the Take 2 already playing");
            s.undo();
            check (at (.3) == second.file, "one undo plays Take 2 again");
        }
        check (s.compTake (laneOne, .5, .25).failed() && s.compTake ("missing", 0, 1).failed(), "comping needs a lane and a forward range");
        ok (s.compTake (laneOne, .25, .5), "comp Take 1 from 0.25 to 0.5 s");
        auto main = sorted (track().clips);
        const auto inRange = [&] (const std::vector<Session::ClipInfo>& clips, double at)
        { for (const auto& c : clips) if (c.start <= at + 1e-9 && c.start + c.length > at) return c; return Session::ClipInfo {}; };
        check (inRange (main, .3).file == first.file && near (inRange (main, .3).start, .25) && near (inRange (main, .3).length, .25)
               && near (inRange (main, .3).sourceOffset - first.sourceOffset, .25 - first.start), "the main lane plays Take 1 in the range, at its own timing");
        check (inRange (main, .1).file == second.file && inRange (main, .7).file == second.file, "and Take 2 around it");
        s.undo();
        check (inRange (sorted (track().clips), .3).file == second.file, "one undo restores the main lane");
        s.redo();
        ok (s.cycleTake (trackId, .25, .5, true), "cycle to the next take");
        check (inRange (sorted (track().clips), .3).file == second.file, "the range now plays Take 2");
        ok (s.cycleTake (trackId, .25, .5, true), "and again");
        check (inRange (sorted (track().clips), .3).file == first.file, "wrapping back to Take 1");
        check (s.cycleTake (trackId, 10, 11, true).failed(), "a range no lane covers cannot cycle");

        // Auditioning: Take 2 plays in place of the main lane, which the snapshot keeps showing.
        const auto hear = [&] { s.seek (.25); ok (s.play(), "play"); auto out = captureSyntheticTestDevice (s, 30); s.stop(); return out; };
        const auto difference = [] (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
        {
            // The first block follows the transport starting, so comparison begins at the second.
            float worst = 0;
            for (int c = 0; c < 2; ++c) for (int n = 256; n < std::min (a.getNumSamples(), b.getNumSamples()); ++n) worst = std::max (worst, std::abs (a.getSample (c, n) - b.getSample (c, n)));
            return worst;
        };
        ok (s.setTrackArmed (0, false), "disarm, so only the clips play"); pumpSyntheticTestDevice (s, 8);
        const auto mainHeard = hear();
        check (mainHeard.getMagnitude (0, mainHeard.getNumSamples()) > .01f && difference (mainHeard, hear()) < 1e-6f, "the main lane plays the same way twice");
        const auto shown = track().clips;
        ok (s.auditionTakeLane (laneTwo), "audition Take 2");
        check (track().auditionLane == laneTwo && track().clips.size() == shown.size() && track().clips[0].id == shown[0].id,
               "the auditioned track still shows its main lane");
        check (difference (mainHeard, hear()) > .01f, "auditioning plays Take 2 where the main lane plays Take 1");
        s.stopAuditioning();
        check (track().auditionLane.isEmpty() && difference (mainHeard, hear()) < 1e-6f, "stopping the audition brings the main lane back exactly");
        ok (s.auditionTakeLane (laneOne), "audition Take 1");
        ok (s.setTrackPan (0, .25f), "an edit while auditioning");
        check (track().auditionLane.isEmpty() && track().pan == .25f && track().clips.size() == shown.size(), "the edit ends the audition and applies to the main lane");
        s.undo();
        check (track().pan == 0 && difference (mainHeard, hear()) < 1e-6f, "undo then leaves the main lane as it was");

        // Managing lanes.
        juce::String added;
        ok (s.insertTakeLane (trackId, added), "insert a take lane");
        check (track().takeLanes.size() == 3 && track().takeLanes[2].clips.empty() && track().takeLanes[2].name == "Take 3", "it is empty and named for its place");
        ok (s.renameTakeLane (added, "Keeper"), "rename it");
        check (track().takeLanes[2].name == "Keeper" && s.renameTakeLane (added, "  ").failed(), "names must not be blank");
        ok (s.removeTakeLane (added), "delete it");
        check (track().takeLanes.size() == 2, "it is gone");
        s.undo(); check (track().takeLanes.size() == 3 && track().takeLanes[2].name == "Keeper", "undo returns it with its name");
        s.undo(); s.undo(); check (track().takeLanes.size() == 2, "and undoes the insert");
        // Delete Unused Take Lanes: a lane is in use while the main lane plays something from it.
        check (s.unusedTakeLanes ({ trackId }).isEmpty(), "both takes are in use: Take 2's pass plays in the main lane, with Take 1's comp and later pass");
        ok (s.insertTakeLane (trackId, added), "insert an empty lane");
        check (s.unusedTakeLanes ({ trackId }) == juce::StringArray { added } && s.unusedTakeLanes ({ "missing" }).isEmpty(), "which is unused; an unknown track has none");
        ok (s.removeTakeLanes (s.unusedTakeLanes ({ trackId })), "delete the unused lanes");
        check (track().takeLanes.size() == 2, "only it goes");
        s.undo(); check (track().takeLanes.size() == 3, "one undo brings it back");
        s.undo(); check (track().takeLanes.size() == 2, "and another undoes the insert");

        // Reopening.
        ok (s.setTrackArmed (0, false), "disarm");
        ok (s.saveProject (project), "save the takes");
        const auto assets = project.getSiblingFile ("Takes Assets");
        check (track().takeLanes[0].clips[0].file.isAChildOf (assets) && track().takeLanes[1].clips[0].file.isAChildOf (assets), "the takes' audio is in the project's assets");
        const auto before = track();
        ok (s.loadProject (project), "reopen");
        const auto after = s.snapshot().tracks[0];
        check (after.takeLanes.size() == 2 && after.takeLanes[0].clips.size() == 2 && after.takeLanes[1].clips[0].file == before.takeLanes[1].clips[0].file
               && after.takeLanes[1].clips[0].file.existsAsFile() && s.missingMedia().isEmpty(), "the lanes reopen with their audio");
        // Loop recording, as the reference's: each pass round a loop becomes a take lane of its own,
        // and the last pass recorded ends up on top in the main lane. A one-second loop, shorter than
        // the engine's own minimum, records 3.5 passes of audio into one file, cut into passes.
        {
            ok (s.setTrackInput (0, s.inputNames()[0]), "route the input again"); ok (s.setTrackArmed (0, true), "and arm");
            pumpSyntheticTestDevice (s, 8);
            const auto lanesBefore = track().takeLanes.size();
            s.setLoop (true, 8, 9);
            take (s, 8, 3.5);
            const auto t = track();
            // The two lanes free after the loop's start are reused, as the reference reuses them, and two more made.
            std::vector<Session::ClipInfo> passes;
            for (const auto& lane : t.takeLanes) for (const auto& c : lane.clips) if (c.start >= 8 - 1e-6) passes.push_back (c);
            std::sort (passes.begin(), passes.end(), [] (const auto& a, const auto& b) { return a.sourceOffset < b.sourceOffset; });
            check (t.takeLanes.size() == lanesBefore + 2 && passes.size() == 4, "3.5 passes fill four take lanes, the two free after the loop's start reused");
            bool laidOut = passes.size() == 4;
            for (size_t k = 0; laidOut && k < 4; ++k)
                laidOut = near (passes[k].start, 8) && near (passes[k].sourceOffset - passes[0].sourceOffset, (double) k) && passes[k].file == passes[0].file
                       && (k < 3 ? near (passes[k].length, 1) : passes[k].length > .4 && passes[k].length < .6);
            check (laidOut, "each lane holds its pass at the loop, one second each and the last a half, in order through one file");
            std::vector<Session::ClipInfo> main;
            for (const auto& c : t.clips) if (c.start >= 8 - 1e-6 && c.start < 9) main.push_back (c);
            main = sorted (main);
            check (main.size() == 2 && near (main[0].start, 8) && near (main[0].sourceOffset, passes[3].sourceOffset) && near (main[0].length, passes[3].length)
                       && near (main[1].start, 8 + passes[3].length) && near (main[1].sourceOffset, passes[2].sourceOffset + passes[3].length) && near (main[1].start + main[1].length, 9),
                   "the main lane plays the last pass, and the third after it");
            s.undo();
            check (track().takeLanes.size() == lanesBefore && track().takeLanes[0].clips.size() == 2, "one undo removes every pass");
            s.redo();

            // Ended with the Record button instead of Stop, as the reference's ends it, the passes are
            // the same and playback carries on round the loop.
            s.seek (8); ok (s.record(), "record round the loop again");
            pumpSyntheticTestDevice (s, (int) std::lround (2.5 * blocksPerSecond));
            ok (s.recordButton(), "press Record to end it");
            check (s.snapshot().playing && s.snapshot().looping && ! s.snapshot().recording, "playback carries on round the loop");
            pumpSyntheticTestDevice (s, blocksPerSecond);
            const auto at = s.snapshot().position;
            s.stop();
            check (at >= 8 && at < 9, "still looping a second later (" + juce::String (at, 3) + " s)");
            std::vector<Session::ClipInfo> again;
            for (const auto& lane : track().takeLanes)
                for (const auto& c : lane.clips) if (c.start >= 8 - 1e-6 && c.file != passes[0].file) again.push_back (c);
            std::sort (again.begin(), again.end(), [] (const auto& a, const auto& b) { return a.sourceOffset < b.sourceOffset; });
            bool split = again.size() == 3;
            for (size_t k = 0; split && k < 3; ++k)
                split = near (again[k].start, 8) && near (again[k].sourceOffset - again[0].sourceOffset, (double) k) && again[k].file == again[0].file
                     && (k < 2 ? near (again[k].length, 1) : again[k].length > .4 && again[k].length < .6);
            check (split, "its 2.5 passes are cut into three lanes, one second each and the last a half (" + juce::String ((int) again.size()) + " passes)");

            // From before the loop, as the reference's does, recording starts where the transport is:
            // the first pass runs from there to the loop's end, then a pass each time round.
            std::set<juce::String> earlier;
            for (const auto& lane : track().takeLanes) for (const auto& c : lane.clips) earlier.insert (c.id);
            s.seek (7); ok (s.record(), "record from a second before the loop");
            pumpSyntheticTestDevice (s, (int) std::lround (2.5 * blocksPerSecond));
            s.stop();
            std::vector<Session::ClipInfo> rolled;
            for (const auto& lane : track().takeLanes)
                for (const auto& c : lane.clips) if (! earlier.count (c.id)) rolled.push_back (c);
            std::sort (rolled.begin(), rolled.end(), [] (const auto& a, const auto& b) { return a.sourceOffset < b.sourceOffset; });
            check (rolled.size() == 2 && near (rolled[0].start, 7) && near (rolled[0].length, 2) && near (rolled[1].start, 8)
                       && near (rolled[1].sourceOffset - rolled[0].sourceOffset, 2) && rolled[1].length > .4 && rolled[1].length < .6,
                   "the first pass runs from 7 s to the loop's end, and the second round the loop (" + [&] { juce::String d; for (const auto& c : rolled) d << juce::String (c.start, 3) << "+" << juce::String (c.length, 3) << "@" << juce::String (c.sourceOffset, 3) << " "; return d.trim(); }() + ")");
            ok (s.setTrackArmed (0, false), "disarm the audio track");
        }
        // MIDI loop recording through the computer keyboard: a note in each pass.
        {
            ok (s.addMidiTrack ("Keys"), "a MIDI track");
            int keys = -1;
            for (int i = 0; i < (int) s.snapshot().tracks.size(); ++i) if (s.snapshot().tracks[(size_t) i].name == "Keys") keys = i;
            ok (s.setComputerKeyboardEnabled (true), "the computer keyboard on");
            ok (s.setTrackInput (keys, Session::computerKeyboardName), "Keys takes it"); ok (s.setTrackArmed (keys, true), "arm Keys");
            pumpSyntheticTestDevice (s, 8);
            s.setLoop (true, 12, 14);
            s.seek (12); ok (s.record(), "record round a two-second loop");
            for (const int pitch : { 60, 62, 64 })
            {
                pumpSyntheticTestDevice (s, blocksPerSecond / 4);
                ok (s.computerKeyboardNote (pitch, .8f, true), "play a note");
                pumpSyntheticTestDevice (s, blocksPerSecond / 4);
                ok (s.computerKeyboardNote (pitch, 0, false), "release it");
                if (pitch != 64) pumpSyntheticTestDevice (s, blocksPerSecond * 3 / 2);
            }
            pumpSyntheticTestDevice (s, blocksPerSecond / 2);
            s.stop(); pumpSyntheticTestDevice (s, 8);
            const auto k = s.snapshot().tracks[(size_t) keys];
            bool lanes = k.takeLanes.size() == 3;
            const int pitches[] { 60, 62, 64 };
            for (size_t l = 0; lanes && l < 3; ++l)
                lanes = k.takeLanes[l].clips.size() == 1 && near (k.takeLanes[l].clips[0].start, 12) && k.takeLanes[l].clips[0].notes.size() == 1
                     && k.takeLanes[l].clips[0].notes[0].pitch == pitches[l] && std::abs (k.takeLanes[l].clips[0].notes[0].startBeat - .5) < .1;
            check (lanes, "three MIDI passes make three lanes, each holding its own note half a beat in");
            int heard = 0; bool onlyLast = true;
            for (const auto& c : k.clips) for (const auto& n : c.notes) { ++heard; onlyLast = onlyLast && n.pitch == 64; }
            check (heard == 1 && onlyLast, "the main lane plays the last pass, its note replacing the earlier ones under it");
            // With Arrangement Overdub on, as the reference's, a take adds its notes to what it records over.
            check (! s.arrangementOverdub(), "Arrangement Overdub is off at first");
            s.setArrangementOverdub (true);
            s.setLoop (false, 12, 14);
            s.seek (12); ok (s.record(), "record over the last pass with Arrangement Overdub on");
            pumpSyntheticTestDevice (s, blocksPerSecond / 4);
            ok (s.computerKeyboardNote (67, .8f, true), "play another note");
            pumpSyntheticTestDevice (s, blocksPerSecond / 4);
            ok (s.computerKeyboardNote (67, 0, false), "release it");
            pumpSyntheticTestDevice (s, blocksPerSecond);
            s.stop(); pumpSyntheticTestDevice (s, 8);
            const auto pitchesOf = [&]
            {
                const auto track = s.snapshot().tracks[(size_t) keys]; // A copy: the snapshot is a temporary.
                std::set<int> pitches;
                for (const auto& c : track.clips) for (const auto& n : c.notes) pitches.insert (n.pitch);
                return pitches;
            };
            const auto merged = pitchesOf();
            check (merged.count (64) == 1 && merged.count (67) == 1, "the main lane keeps the earlier note with the new one");
            s.undo();
            const auto before = pitchesOf();
            check (before == std::set<int> { 64 }, "one undo takes the overdub back");
            s.setArrangementOverdub (false);
            ok (s.setTrackArmed (keys, false), "disarm Keys");
        }
        std::cout << "Take lanes passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL take lanes: " << e.what() << std::endl; return 1; }
}
}
