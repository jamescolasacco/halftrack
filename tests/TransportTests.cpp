#include "Session.h"
#include <cmath>
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
        check (s.snapshot().playing && played > 2.5 && played < 3 + slack, "playback starts from the insert marker");

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
        ok (s.setTrackArmed (0, false), "disarm the track");

        // The marker keeps its beat when the tempo changes: beat 6 is 3 s at 120 BPM and 6 s at 60 BPM.
        s.setTempo (60);
        check (near (s.snapshot().insertMarker, 6, 1e-3), "the insert marker keeps its beat through a tempo change");
        std::cout << "Transport passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL transport: " << e.what() << std::endl; return 1; }
}
}
