#pragma once

#include "Session.h"
#include <functional>

// The Arrangement's automation envelopes. A track offers its mixer's Volume, Pan and sends, every
// control of its native devices (racks' inner devices included) and its plug-ins' parameters. Envelope
// draws one of them across a lane and edits its breakpoints as the reference does: clicking a
// segment adds a breakpoint, double-clicking empty space adds one there, clicking a breakpoint
// deletes it and dragging moves it. Option-dragging a segment (Alt elsewhere) curves it, and
// Option-double-clicking it straightens it. Shift-dragging a segment moves it. Dragging a
// breakpoint inside the time selection moves every breakpoint in it, and Simplify Envelope
// removes the selection's breakpoints the shape does not need.
namespace halftrack::arrangementAutomation
{
struct Target
{
    enum class Kind { volume, pan, send, native, external };
    Kind kind = Kind::volume;
    juce::String key;           // The Session lane key: "mixer:<track>:volume|pan", "send:<track>:<return>" or "device:<plugin>:<parameter>".
    juce::String device, control; // Chooser labels, such as "Mixer" and "Volume".
    juce::String trackId, pluginId, returnId; // A send's return track.
    int parameter = 0;
    Session::EffectKind effect {};
    Session::AutomationLane lane;
    float manual = 0;           // The destination's value without automation, in its own units.
};

// A send is named for its return, as `tracks` (the project's tracks) shows it.
std::vector<Target> targets (Session&, const Session::TrackInfo&, const std::vector<Session::TrackInfo>& tracks);
std::optional<Target> find (Session&, const Session::TrackInfo&, const std::vector<Session::TrackInfo>& tracks, const juce::String& key);
juce::String volumeKey (const Session::TrackInfo&);
float toProportion (const Target&, float value);
float fromProportion (const Target&, float proportion);
juce::String format (const Target&, float value);
juce::Result write (Session&, const Target&, const Session::AutomationLane&);

class Envelope
{
public:
    // The arrangement's mapping between beats and lane x, and its grid.
    std::function<float (double)> xOfBeat;
    std::function<double (float)> beatOfX;
    std::function<double (double)> snapBeat;
    // The time selection on a track, in beats, when it has one.
    std::function<std::optional<std::pair<double, double>> (const juce::String& trackId)> selectedBeats;

    void paint (juce::Graphics&, juce::Rectangle<float> area, const Target&) const;
    // Each returns true when it handled the event.
    bool mouseDown (Session&, const Target&, juce::Rectangle<float> area, const juce::MouseEvent&);
    bool mouseDrag (Session&, const Target&, juce::Rectangle<float> area, const juce::MouseEvent&);
    bool mouseUp (Session&, const Target&);
    bool doubleClick (Session&, const Target&, juce::Rectangle<float> area, const juce::MouseEvent&);
    bool active() const noexcept { return gesture; }
    // The lane with its breakpoints inside [from, to] thinned to those the shape needs: each kept
    // point lies more than `tolerance` (in 0-1 proportions) from the line through its neighbours.
    static Session::AutomationLane simplified (const Target&, double from, double to, float tolerance = .005f);
    static constexpr float hitRadius = 5;

private:
    int hitPoint (const Target&, juce::Rectangle<float> area, juce::Point<float>) const;
    // The segment under the point, as the index of its right-hand breakpoint (points.size() after the last).
    std::optional<size_t> hitSegment (const Target&, juce::Rectangle<float> area, juce::Point<float>) const;
    // The line's y at an x on the segment ending at `right`, following its curve.
    float yAt (const Target&, juce::Rectangle<float> area, size_t right, float x) const;
    juce::Point<float> position (const Target&, juce::Rectangle<float> area, const Session::AutomationPoint&) const;
    Session::AutomationPoint pointAt (const Target&, juce::Rectangle<float> area, juce::Point<float>, bool snap) const;
    void begin (Session&, const Target&);
    int dragged = -1;           // The breakpoint being dragged.
    int curving = -1;           // The breakpoint whose segment an Option-drag is curving.
    float curveFrom = 0;
    int segment = -1;           // The breakpoint whose segment a Shift-drag is moving.
    // A drag's breakpoints as they were, and those moving with the one dragged.
    std::vector<Session::AutomationPoint> original;
    std::vector<size_t> group;
    bool moved = false, gesture = false, pressedPoint = false;
    juce::String gestureKey;
    juce::Point<float> downAt;
};
}
