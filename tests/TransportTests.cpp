#include "Session.h"
#include <cmath>
#include <set>
#include <iostream>
#include <stdexcept>

// Play, Stop, Continue and the insert marker, through the synthetic device. Play starts from the
// insert marker and Stop leaves the play position where it stopped; Continue plays on from there.
// Locating moves both; placing the marker while playing leaves playback running. Recording starts
// from the marker too, and the marker keeps its beat through a tempo change. None of this is an
// edit: it neither dirties the project nor enters the undo history.
namespace halftrack
{
std::unique_ptr<juce::AudioIODeviceType> makeSyntheticTestDeviceType();
float pumpSyntheticTestDevice (Session&, int blocks);
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS transport: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
constexpr int blocksPerSecond = 48000 / 256;
// The engine reports positions through a timer, so positions after pumping are approximate.
constexpr double slack = .15;
bool near (double a, double b, double tolerance = 1e-6) { return std::abs (a - b) <= tolerance; }
}

int runTransportTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("transport-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        Session s (false, makeSyntheticTestDeviceType());
        pumpSyntheticTestDevice (s, 8);
        check (s.snapshot().insertMarker == 0 && s.snapshot().position == 0, "a new project's insert marker and play position are at the start");

        s.setInsertMarker (2);
        check (near (s.snapshot().insertMarker, 2) && near (s.snapshot().position, 2), "placing the insert marker while stopped moves the play position with it");
        ok (s.play(), "Play");
        pumpSyntheticTestDevice (s, blocksPerSecond);
        const auto played = s.snapshot().position;
        check (s.snapshot().playing && played > 2.5 && played < 3 + slack, "playback starts from the insert marker (" + juce::String (played, 3) + " s after a second)");

        s.setInsertMarker (1);
        pumpSyntheticTestDevice (s, blocksPerSecond / 4);
        check (s.snapshot().playing && s.snapshot().position > played && near (s.snapshot().insertMarker, 1), "placing the insert marker while playing leaves playback running");

        s.stop();
        const auto stopped = s.snapshot().position;
        check (! s.snapshot().playing && stopped > played && near (s.snapshot().insertMarker, 1), "Stop leaves the play position where playback stopped");
        ok (s.continuePlayback(), "Continue");
        check (s.snapshot().playing && s.snapshot().position >= stopped - 1e-6, "Continue plays on from where playback stopped");
        pumpSyntheticTestDevice (s, blocksPerSecond / 4);
        s.stop();
        ok (s.play(), "Play again");
        check (near (s.snapshot().position, 1), "Play returns to the insert marker, not where playback stopped");
        pumpSyntheticTestDevice (s, blocksPerSecond / 4);

        s.seek (5);
        pumpSyntheticTestDevice (s, 4);
        check (s.snapshot().playing && s.snapshot().position >= 5 && s.snapshot().position < 5 + slack && near (s.snapshot().insertMarker, 5),
               "locating while playing jumps playback and moves the insert marker");
        s.stop(); s.returnToStart();
        check (s.snapshot().insertMarker == 0 && s.snapshot().position == 0, "Return to Start puts the marker and play position at the beginning");
        check (! s.snapshot().dirty && ! s.canUndo(), "moving the insert marker and playing are not edits");

        // Recording starts from the insert marker.
        ok (s.saveProject (root.getChildFile ("Transport").getChildFile ("Transport.halftrack")), "save a recording destination");
        ok (s.setTrackInput (0, s.inputNames()[0]), "route the input"); ok (s.setTrackArmed (0, true), "arm the track");
        s.setInsertMarker (3); s.stop();
        ok (s.continuePlayback(), "play away from the marker");
        // Arming rebuilds the graph, which can hold playback back for some blocks on a slow machine.
        for (int i = 0; i < 40 && s.snapshot().position <= 3.3; ++i) pumpSyntheticTestDevice (s, blocksPerSecond / 4);
        s.stop();
        check (s.snapshot().position > 3.3, "playback stopped after the marker");
        ok (s.record(), "Record");
        pumpSyntheticTestDevice (s, blocksPerSecond);
        s.stop();
        const auto clips = s.snapshot().tracks[0].clips;
        check (clips.size() == 1 && std::abs (clips[0].start - 3) < .01, "the take starts at the insert marker");

        // Stopped before the device's next block, a recording leaves the transport stopped: the engine
        // started its play head on that block regardless, and its transport then played along.
        ok (s.record(), "Record and stop at once");
        s.stop();
        bool stayedStopped = true;
        for (int i = 0; i < 20; ++i)
        {
            pumpSyntheticTestDevice (s, blocksPerSecond / 20);
            juce::Thread::sleep (10); // The transport's timer, which started playback, runs by the clock.
            stayedStopped = stayedStopped && ! s.snapshot().playing && ! s.snapshot().recording;
        }
        check (stayedStopped, "a recording stopped before its first block does not start playback afterwards");

        // The Record button, as the reference's: pressed while recording it ends the take and playback
        // carries on; pressed while playing it records from there.
        std::set<juce::String> seen;
        const auto newTakes = [&]
        {
            std::vector<Session::ClipInfo> taken;
            const auto now = s.snapshot();
            for (const auto& c : now.tracks[0].clips) if (seen.insert (c.id).second) taken.push_back (c);
            return taken;
        };
        newTakes(); // Those so far.
        s.seek (0); ok (s.record(), "Record from the start");
        pumpSyntheticTestDevice (s, blocksPerSecond / 2);
        ok (s.recordButton(), "press Record while recording");
        auto taken = newTakes();
        check (s.snapshot().playing && ! s.snapshot().recording && taken.size() == 1 && taken[0].start < .05 && taken[0].length > .4 && taken[0].length < .65,
               "the take ends where Record was pressed, and playback carries on (" + juce::String (s.snapshot().playing ? "playing" : "stopped") + ", "
                   + juce::String ((int) taken.size()) + " takes" + (taken.empty() ? juce::String() : ", " + juce::String (taken[0].start, 3) + " s for " + juce::String (taken[0].length, 3)) + ")");
        pumpSyntheticTestDevice (s, blocksPerSecond / 4);
        const auto punchedAt = s.snapshot().position;
        check (s.snapshot().playing && punchedAt > .6, "playback goes on");
        ok (s.recordButton(), "press Record while playing");
        check (s.snapshot().recording, "it records from there");
        pumpSyntheticTestDevice (s, blocksPerSecond / 2);
        s.stop();
        taken = newTakes();
        check (taken.size() == 1 && taken[0].start > punchedAt - .05 && taken[0].start < punchedAt + .1 && taken[0].length > .4 && taken[0].length < .65,
               "the take starts where Record was pressed during playback (" + juce::String (taken.empty() ? -1.0 : taken[0].start, 3) + " s, pressed at " + juce::String (punchedAt, 3) + ")");

        // Start Playback with Record, on by default as the reference's. Off, Record waits, lit, for Play
        // or a Session launch; pressed again, or Stop, it stops waiting; Shift reverses the preference.
        auto prefs = s.preferences();
        check (prefs.startPlaybackWithRecord, "Start Playback with Record is on by default");
        prefs.startPlaybackWithRecord = false; ok (s.setPreferences (prefs), "switch Start Playback with Record off");
        s.setInsertMarker (2); // Clear of the takes so far, which a take over them would split.
        ok (s.record(), "Record while stopped");
        pumpSyntheticTestDevice (s, blocksPerSecond / 4);
        check (s.snapshot().recordWaiting && ! s.snapshot().playing && ! s.snapshot().recording, "Record waits without starting playback");
        ok (s.recordButton(), "press Record again"); check (! s.snapshot().recordWaiting && ! s.snapshot().playing, "pressing it again stops waiting");
        ok (s.recordButton(), "and once more"); s.stop(); check (! s.snapshot().recordWaiting, "Stop stops it waiting too");
        ok (s.recordButton(), "Record to wait again");
        ok (s.play(), "Play");
        check (s.snapshot().playing && s.snapshot().recording && ! s.snapshot().recordWaiting, "Play starts the waiting recording");
        pumpSyntheticTestDevice (s, blocksPerSecond / 2);
        s.stop();
        taken = newTakes();
        check (taken.size() == 1 && std::abs (taken[0].start - 2) < .01, "it records from the insert marker");
        ok (s.recordButton (true), "Shift-press Record");
        check (s.snapshot().playing && s.snapshot().recording, "Shift reverses the preference: recording and playback start at once");
        pumpSyntheticTestDevice (s, blocksPerSecond / 4);
        s.stop(); newTakes();
        prefs.startPlaybackWithRecord = true; ok (s.setPreferences (prefs), "switch it on again");
        ok (s.recordButton (true), "Shift-press Record");
        check (s.snapshot().recordWaiting && ! s.snapshot().playing, "with the preference on, Shift makes Record wait");
        s.stop();
        prefs.startPlaybackWithRecord = false; ok (s.setPreferences (prefs), "and off");
        ok (s.addMidiTrack ("Launcher"), "a track to launch from");
        const int launcher = (int) s.snapshot().tracks.size() - 1;
        ok (s.addMidiClipToSlot (s.snapshot().tracks[(size_t) launcher].id, 0), "a Session clip");
        ok (s.record(), "Record to wait");
        ok (s.launchSlot (launcher, 0), "launch the clip");
        check (s.snapshot().playing && s.snapshot().recording && ! s.snapshot().recordWaiting, "launching a Session clip starts the waiting recording");
        pumpSyntheticTestDevice (s, blocksPerSecond / 4);
        s.stop(); newTakes();
        ok (s.resetPreferences(), "reset the preferences");
        s.setInsertMarker (3);
        ok (s.setTrackArmed (0, false), "disarm the track");

        // With the loop on, playback starts from the insert marker, as the reference's does (static
        // reading of its code): from before the loop it rolls into it, from past its end it plays on.
        {
            s.setLoop (true, 6, 7);
            const auto at = [&] (double seconds) { pumpSyntheticTestDevice (s, (int) std::lround (seconds * blocksPerSecond)); return s.snapshot().position; };
            // A slow machine can take a moment to start playing; this waits until the play head moves.
            const auto started = [&] (double from)
            {
                for (int i = 0; i < 200 && std::abs (s.snapshot().position - from) < 1e-3; ++i) pumpSyntheticTestDevice (s, 4);
                return s.snapshot().position;
            };
            s.setInsertMarker (5);
            ok (s.play(), "play from before the loop");
            const auto rolling = started (5);
            check (rolling > 5 && rolling < 6, "it plays from the insert marker, not the loop's start (" + juce::String (rolling, 3) + " s)");
            const auto looped = at (2);
            check (looped >= 6 && looped < 7, "and loops once inside the loop (" + juce::String (looped, 3) + " s)");
            s.stop();
            s.setInsertMarker (8);
            ok (s.play(), "play from past the loop's end");
            started (8);
            const auto past = at (1.5);
            check (past > 9 && past < 10 + slack, "it plays on without looping (" + juce::String (past, 3) + " s)");
            s.seek (5.5);
            const auto back = at (1.5);
            check (back >= 6 && back < 7, "a jump before the loop rolls into it again (" + juce::String (back, 3) + " s)");
            s.seek (7.5);
            started (7.5); // A loaded machine resumes a moment after the jump.
            const auto beyond = at (.25);
            // Looping would bring it back inside [6, 7); a loaded machine can report the move late.
            check (beyond > 7.5 && beyond < 9, "and a jump past it plays on (" + juce::String (beyond, 3) + " s)");
            s.stop(); s.setLoop (false, 6, 7); s.setInsertMarker (3);
            // The loop brace's Set Song Start Time Here: Play starts at the loop's start, loop on or off.
            check (! s.snapshot().loopIsSongStart, "the loop is not the song's start at first");
            ok (s.setLoopIsSongStart (true), "Set Song Start Time Here");
            ok (s.play(), "Play");
            const auto fromLoop = started (3);
            check (s.snapshot().loopIsSongStart && fromLoop >= 6 && fromLoop < 7 && near (s.snapshot().insertMarker, 3),
                   "it plays from the loop's start, the loop off, leaving the insert marker (" + juce::String (fromLoop, 3) + " s)");
            s.stop(); s.undo();
            check (! s.snapshot().loopIsSongStart, "it is one undo step");
            ok (s.play(), "Play again");
            const auto fromMarker = started (fromLoop);
            check (fromMarker >= 3 && fromMarker < 4, "and Play starts at the insert marker again (" + juce::String (fromMarker, 3) + " s)");
            s.stop(); s.setInsertMarker (3);
            // A locator's Set Song Start Time Here: Play starts at the locator, which takes the song's
            // start from the loop; moving the locator moves it, and deleting it clears it.
            juce::String songLocator, otherLocator;
            ok (s.addLocator (4.5, "Song", songLocator), "a locator at 4.5 s");
            ok (s.addLocator (2, "Other", otherLocator), "and another at 2 s");
            ok (s.setLoopIsSongStart (true), "the loop is the song's start");
            ok (s.setLocatorSongStart (songLocator, true), "Set Song Start Time Here on the locator");
            const auto holder = [&]
            {
                juce::String which = s.snapshot().loopIsSongStart ? "loop" : "";
                for (const auto& l : s.snapshot().locators) if (l.songStart) which += l.id;
                return which;
            };
            check (holder() == songLocator, "the locator takes the song's start from the loop");
            ok (s.play(), "Play");
            const auto fromLocator = started (3);
            check (fromLocator >= 4.5 && fromLocator < 5.5 && near (s.snapshot().insertMarker, 3), "it plays from the locator, leaving the insert marker (" + juce::String (fromLocator, 3) + " s)");
            s.stop();
            ok (s.updateLocator (songLocator, "Song", 1), "move the locator to 1 s");
            const auto stoppedAt = s.snapshot().position;
            ok (s.play(), "Play");
            const auto fromMoved = started (stoppedAt);
            check (fromMoved >= 1 && fromMoved < 2, "the song's start moves with it (" + juce::String (fromMoved, 3) + " s)");
            s.stop();
            ok (s.setLocatorSongStart (otherLocator, true), "Set Song Start Time Here on the other locator");
            check (holder() == otherLocator, "one locator holds it at a time");
            s.undo();
            check (holder() == songLocator, "it is one undo step");
            ok (s.setLoopIsSongStart (true), "the loop's Set Song Start Time Here");
            check (holder() == "loop", "the loop takes it back from the locator");
            ok (s.setLocatorSongStart (songLocator, true), "the locator again");
            ok (s.removeLocator (songLocator), "delete the locator");
            check (holder().isEmpty(), "deleting it clears the song's start");
            const auto beforePlay = s.snapshot().position;
            ok (s.play(), "Play");
            const auto fromInsert = started (beforePlay);
            check (fromInsert >= 3 && fromInsert < 4, "and Play starts at the insert marker (" + juce::String (fromInsert, 3) + " s)");
            s.stop(); ok (s.removeLocator (otherLocator), "remove the other locator"); s.setInsertMarker (3);
        }

        // The marker keeps its beat when the tempo changes: beat 6 is 3 s at 120 BPM and 6 s at 60 BPM.
        s.setTempo (60);
        check (near (s.snapshot().insertMarker, 6, 1e-3), "the insert marker keeps its beat through a tempo change");
        std::cout << "Transport passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL transport: " << e.what() << std::endl; return 1; }
}
}
