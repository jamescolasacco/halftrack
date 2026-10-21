#include "Session.h"
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
        std::cout << "Take lanes passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL take lanes: " << e.what() << std::endl; return 1; }
}
}
