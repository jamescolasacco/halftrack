#pragma once
#include <optional>

// MIDI takeover, ported from the research model of the reference's controller arithmetic (static reading of its code;
// docs/research/midi-mapping-contract.md): what an absolute controller does to the control it is mapped to when the
// control's value and the controller's position differ. As the reference's, everything is single precision, in the
// control's own scale between the mapping's lower and upper ends.
namespace halftrack::takeover
{
enum class Mode { none, pickUp, valueScaling };

// The control's value for a controller value of 0 to 127 (0x1019c7678): from lower to upper, or upper to lower when the
// mapping is reversed.
float absolute (float value, float lower, float upper, bool reverse) noexcept;

struct Move
{
    float value = 0;     // The control's next value, within the mapping's range.
    bool pickUp = false; // Pick-Up judged the move, as the reference reports each one: picked up when value is the controller's.
};
// One controller message (0x1019cd538): the control's value now, the controller's previous value from the same input,
// channel and controller (none while it is not known), and its value now. None jumps to the controller. Pick-Up and
// Value Scaling follow a controller already in step with the control; otherwise Pick-Up waits until the controller
// passes the control's value, and Value Scaling moves the value toward the end the controller moves to, in proportion,
// so both reach it together. With no previous value they leave the control as it is.
Move apply (Mode, float current, std::optional<float> prior, float value, float lower, float upper, bool reverse) noexcept;
}
