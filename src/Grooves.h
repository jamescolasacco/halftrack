#pragma once

#include "Session.h"

// Grooves after the reference's groove pool. A groove's events are measured against its Base grid:
// each event belongs to its nearest grid line, and its distance from that line is the line's timing
// offset. A note is pulled toward its nearest grid line by Quantize, then moved by that line's
// offset scaled by Timing, jittered by up to half a grid step by Random, and accented by Velocity
// relative to the groove's loudest event. The global amount scales Timing, Random and Velocity.
// This is Halftrack's model of the documented controls; the reference's exact arithmetic is unmeasured.
namespace halftrack::grooves
{
double baseBeats (Session::GrooveBase) noexcept;
// Names, Base, length, amounts and ordered in-range events.
bool valid (const Session::GrooveInfo&) noexcept;

// A groove prepared for placing notes: one slot per Base grid line in its length.
class Pattern
{
public:
    Pattern (const Session::GrooveInfo&, float globalAmountPercent);
    struct Placement { double beat; int velocity; };
    // A note's start and velocity (1-127) under the groove. The same seed gives the same Random jitter.
    Placement place (double beat, int velocity, uint64_t seed) const noexcept;
    // True when no setting moves or accents any note.
    bool neutral() const noexcept;
private:
    struct Slot { bool used = false; double offset = 0, accent = 0; };
    std::vector<Slot> slots;
    double step = .25, quantize = 0, timing = 0, random = 0, velocity = 0;
};
// Random jitter seed for a note: the clip identity, the pitch and the note's beat in the clip.
uint64_t noteSeed (uint64_t clip, int pitch, double beat) noexcept;
// A groove from a clip's active notes in [0, lengthBeats): chords become one event at their loudest velocity.
Session::GrooveInfo extract (const juce::String& name, const std::vector<Session::MidiNoteInfo>& notes, double lengthBeats);
std::vector<Session::GrooveInfo> library();
}
